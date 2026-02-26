# 280x240 屏幕显示问题诊断报告与解决方案

## 一、问题确认

### 用户描述的问题
1. **动画移到了左上角** - 应该居中显示
2. **动画背景颜色显示为紫色(0xF81F)** - 应该透明显示底层背景
3. **除了字幕UI外全黑** - 背景图没有正确显示

### 目标效果
```
280×240 屏幕
┌─────────────────────────────────┐ Y=0
│         黑色边框 25px            │
├─────────────────────────────────┤ Y=25
│   背景图 280×190 (base.png)      │
│   ┌─────────────────────────┐   │
│   │   动画 160×128          │   │
│   │   居中于背景图           │   │
│   │   透明部分显示背景       │   │
│   └─────────────────────────┘   │
├─────────────────────────────────┤ Y=215
│         黑色边框 25px            │
└─────────────────────────────────┘ Y=240
```

---

## 二、代码架构分析

### 关键文件
| 文件 | 作用 |
|------|------|
| `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc` | 板子配置，包含完整的合成逻辑 |
| `main/images/animation_loader.h/cc` | 动画帧解码器 |
| `main/images/background_loader.h/cc` | 背景图加载器 |
| `main/boards/waveshare-c6-lcd-1.69/config.h` | 硬件配置 |

### 核心常量 (esp32-c6-lcd-1.69.cc)
```cpp
#define COMPOSITE_WIDTH   280           // 合成图宽度
#define COMPOSITE_HEIGHT  190           // 合成图高度
#define ANIM_OFFSET_IN_COMPOSITE_X  60  // (280-160)/2 动画在合成图中的X偏移
#define ANIM_OFFSET_IN_COMPOSITE_Y  31  // (190-128)/2 动画在合成图中的Y偏移
#define COMPOSITE_SCREEN_Y  25          // (240-190)/2 合成图在屏幕上的Y偏移
#define CHROMA_KEY_RGB565  0xF81F       // 紫色透明色
```

### 合成逻辑位置
文件：`esp32-c6-lcd-1.69.cc:617-670`

```cpp
// 核心合成代码
for (uint16_t y = 0; y < COMPOSITE_HEIGHT; y++) {
    // 获取背景行
    if (static_bg_buffer != nullptr) {
        bg_row = &static_bg_buffer[y * COMPOSITE_WIDTH];  // RAM中的背景
    } else {
        bg_loader.DecodeRow(...);  // 从Flash读取背景
    }

    // 检查是否在动画区域
    bool in_anim_y = (y >= ANIM_OFFSET_IN_COMPOSITE_Y && ...);

    for (uint16_t x = 0; x < COMPOSITE_WIDTH; x++) {
        if (在动画区域内) {
            uint16_t anim_pixel = ...;
            if (anim_pixel == CHROMA_KEY_RGB565) {
                out_pixel = bg_row[x];  // 透明→显示背景
            } else {
                out_pixel = anim_pixel; // 不透明→显示动画
            }
        } else {
            out_pixel = bg_row[x];  // 动画区域外→显示背景
        }
        composite_buffer[...] = out_pixel;
    }
}
```

---

## 三、问题诊断

### 诊断点1：背景图加载状态
**检查 `init_static_background()` 是否成功执行**

日志关键字搜索：
```
"Background offset calculated"
"Background loader ready"
"Composite buffer allocated"
"Full background decoded successfully"
```

**可能的失败原因：**
1. `AnimationLoader` 初始化失败 → 无法计算背景偏移
2. `assets` 分区不存在或损坏
3. 背景文件 `backgrounds.bin` 未烧录
4. 内存不足，无法分配 `composite_buffer` (106KB)

### 诊断点2：动画位置问题
**检查 LVGL 图像位置设置**

代码位置：`esp32-c6-lcd-1.69.cc:887-888`
```cpp
lv_obj_set_pos(anim_mgr.bg_image, 0, COMPOSITE_SCREEN_Y);  // 应为 (0, 25)
```

**可能的问题：**
1. `COMPOSITE_SCREEN_Y` 值不正确
2. LVGL 坐标系与预期不同
3. 图像被其他代码移动

### 诊断点3：紫色显示问题
**透明色未被替换的原因**

1. `composite_buffer == nullptr` → 合成逻辑被跳过
2. `bg_row_buffer == nullptr` → 无法读取背景
3. `static_bg_buffer == nullptr` 且 Flash读取失败

当合成失败时，代码回退到直接显示动画帧：
```cpp
// esp32-c6-lcd-1.69.cc:682-690
if (!use_composite) {
    // 回退模式：只显示160x128动画，紫色透明色会直接显示！
    anim_mgr.frame_dsc.header.w = ANIM_FRAME_WIDTH;   // 160
    anim_mgr.frame_dsc.header.h = ANIM_FRAME_HEIGHT;  // 128
    anim_mgr.frame_dsc.data = frame_data;  // 包含紫色像素的原始帧
}
```

---

## 四、解决方案

### 方案A：添加诊断日志（推荐先执行）

在 `esp32-c6-lcd-1.69.cc` 中添加以下日志：

#### 1. 在 `init_static_background()` 开头添加：
```cpp
static void init_static_background(void) {
    ESP_LOGI(TAG, "=== init_static_background START ===");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    // ... 原有代码 ...
```

#### 2. 在 `animation_timer_callback()` 合成逻辑处添加：
```cpp
// 在 use_composite 判断后添加
static bool logged_composite_status = false;
if (!logged_composite_status) {
    ESP_LOGI(TAG, "=== Composite Status ===");
    ESP_LOGI(TAG, "composite_buffer: %p", composite_buffer);
    ESP_LOGI(TAG, "bg_row_buffer: %p", bg_row_buffer);
    ESP_LOGI(TAG, "static_bg_buffer: %p", static_bg_buffer);
    ESP_LOGI(TAG, "use_composite: %d", use_composite ? 1 : 0);
    logged_composite_status = true;
}
```

#### 3. 检查 LVGL 图像位置：
```cpp
// 在 lv_image_set_src() 后添加
static bool logged_image_pos = false;
if (!logged_image_pos && anim_mgr.bg_image) {
    lv_area_t coords;
    lv_obj_get_coords(anim_mgr.bg_image, &coords);
    ESP_LOGI(TAG, "Image position: (%d,%d)-(%d,%d)",
             coords.x1, coords.y1, coords.x2, coords.y2);
    logged_image_pos = true;
}
```

### 方案B：确保背景文件已烧录

#### 1. 检查 assets 分区配置
查看 `partitions.csv`：
```csv
assets,  data, spiffs, 0x800000, 8M,
```

#### 2. 确认数据文件结构
assets 分区应包含：
```
[frames.bin]     - 动画帧数据 (约 5-10MB)
[backgrounds.bin] - 背景图数据 (约 100KB-1MB)
```

#### 3. 烧录命令示例
```bash
# 烧录 assets 分区
esptool.py --chip esp32c6 write_flash 0x800000 assets.bin
```

### 方案C：内存优化（如果内存不足）

#### 检查内存需求：
| 缓冲区 | 大小 | 用途 |
|--------|------|------|
| composite_buffer | 106,400 bytes | 合成输出 |
| static_bg_buffer | 106,400 bytes | 背景图缓存（可选） |
| bg_row_buffer | 560 bytes | 背景行缓冲 |
| decode_buffer | 40,960 bytes | 动画帧解码 |

#### 如果内存不足：
1. 不分配 `static_bg_buffer`，使用逐行从Flash读取
2. 减少其他组件的内存使用
3. 启用 PSRAM（如果硬件支持）

### 方案D：强制启用合成模式

如果合成缓冲区分配失败但希望继续尝试，可以修改代码：

```cpp
// 在 init_static_background() 中，如果 composite_buffer 分配失败
if (!composite_buffer) {
    // 尝试更小的分配策略
    ESP_LOGW(TAG, "Trying smaller composite buffer...");

    // 可以尝试只分配需要合成的行（动画覆盖区域）
    // 或者使用逐行合成模式
}
```

---

## 五、快速验证步骤

### 步骤1：检查日志输出
编译运行后，搜索以下日志：

**成功的日志序列：**
```
I AnimLoader: Animation loader initialized
I waveshare_lcd_1_69: Background offset calculated: XXXXX bytes
I BackgroundLoader: Background loader initialized
I waveshare_lcd_1_69: Composite buffer allocated: 106400 bytes
I waveshare_lcd_1_69: Full background decoded successfully
I waveshare_lcd_1_69: Animation timer started
```

**失败的日志示例：**
```
E AnimLoader: Failed to read header  → frames.bin未烧录
E BackgroundLoader: Invalid magic    → backgrounds.bin未烧录或损坏
E waveshare_lcd_1_69: Failed to allocate composite buffer → 内存不足
```

### 步骤2：验证数据文件
使用 esptool 读取 assets 分区头部：
```bash
esptool.py --chip esp32c6 read_flash 0x800000 64 header.bin
xxd header.bin
```

应该看到：
```
0000: 53 46 xx xx ...  # 'SF' magic (0x5346)
或
0000: 46 53 xx xx ...  # 'FS' magic (0x4653)
```

### 步骤3：测试纯色背景
临时修改代码，用纯色替代背景图测试位置：

```cpp
// 在合成循环前添加
// 临时：用灰色填充合成缓冲区测试位置
if (composite_buffer) {
    memset(composite_buffer, 0x7BEF, COMPOSITE_WIDTH * COMPOSITE_HEIGHT * 2);
}
```

如果灰色矩形显示在正确位置 (0, 25)，说明位置逻辑正确，问题在背景加载。

---

## 六、根本原因总结

根据代码分析，最可能的根本原因是：

### 1. 背景文件未烧录或损坏（最可能）
**症状**：全黑背景 + 紫色透明色显示
**验证**：检查 `BackgroundLoader` 初始化日志
**解决**：重新生成并烧录 `backgrounds.bin`

### 2. 合成缓冲区分配失败
**症状**：动画可能偏移 + 紫色透明色显示
**验证**：检查 `composite_buffer` 是否为 nullptr
**解决**：优化内存或减少其他组件内存使用

### 3. LVGL图像位置被覆盖
**症状**：动画在左上角
**验证**：检查其他代码是否修改了 `bg_image` 位置
**解决**：确保 `lv_obj_set_pos(anim_mgr.bg_image, 0, 25)` 生效

---

## 七、背景图文件生成

如果需要重新生成 `backgrounds.bin`：

### 文件格式
```
Header (12 bytes):
  uint16_t magic;        // 0x4653 或 0x5346
  uint16_t version;      // 1=4bit, 2=8bit
  uint16_t width;        // 280
  uint16_t height;       // 190
  uint8_t  colors;       // 调色板颜色数
  uint8_t  bg_color_idx; // 背景色索引
  uint16_t frame_count;  // 背景图数量

Palette (colors * 2 bytes):
  uint16_t palette[];    // RGB565 调色板

Frames (frame_count * frame_size bytes):
  uint8_t frame_data[];  // 8bit索引或4bit索引
```

### Python 生成脚本示例
```python
# generate_background.py
from PIL import Image
import struct

def convert_to_indexed(img, max_colors=64):
    """将图片转换为索引色"""
    img = img.convert('P', palette=Image.ADAPTIVE, colors=max_colors)
    return img

def rgb888_to_rgb565(r, g, b):
    """RGB888 转 RGB565"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

def generate_backgrounds_bin(input_png, output_bin):
    img = Image.open(input_png)
    img = img.resize((280, 190))  # 调整尺寸
    indexed = convert_to_indexed(img, 64)

    palette = indexed.getpalette()[:64*3]  # RGB palette
    palette_rgb565 = []
    for i in range(0, len(palette), 3):
        rgb565 = rgb888_to_rgb565(palette[i], palette[i+1], palette[i+2])
        palette_rgb565.append(rgb565)

    # 写入文件
    with open(output_bin, 'wb') as f:
        # Header
        f.write(struct.pack('<HHHH', 0x4653, 2, 280, 190))  # magic, version, w, h
        f.write(struct.pack('<BBH', 64, 0, 1))  # colors, bg_idx, frame_count

        # Palette
        for c in palette_rgb565:
            f.write(struct.pack('<H', c))

        # Frame data (8-bit indexed)
        f.write(bytes(indexed.getdata()))

    print(f"Generated {output_bin}: 280x190, 64 colors")

if __name__ == '__main__':
    generate_backgrounds_bin('base.png', 'backgrounds.bin')
```

---

## 八、验证完成的检查清单

- [ ] AnimationLoader 初始化成功（日志确认）
- [ ] BackgroundLoader 初始化成功（日志确认）
- [ ] composite_buffer 分配成功（日志确认指针非空）
- [ ] 背景图解码成功（日志确认）
- [ ] LVGL图像位置正确 (0, 25)
- [ ] 合成逻辑执行（use_composite = true）
- [ ] 透明色正确替换为背景像素
- [ ] 动画居中显示在 280x190 区域中央

---

*文档版本: 2.0*
*创建日期: 2025-01-16*

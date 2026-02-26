# WS2811/WS2812 LED 控制库

这是一个基于 ESP-IDF `led_strip` 组件的 WS2811/WS2812 LED 控制库，提供简单易用的 API。

## 支持的 LED 型号

- WS2811
- WS2812
- WS2812B
- SK6812
- APA106

## 功能特性

- ✅ 简单易用的 API
- ✅ 支持单个或多个 LED 控制
- ✅ 全局亮度控制
- ✅ HSV 到 RGB 颜色转换
- ✅ 预定义常用颜色
- ✅ 丰富的示例代码

## 快速开始

### 1. 基本使用

```c
#include "ws2811.h"

// 配置 LED
ws2811_config_t config = {
    .gpio_num = 48,          // GPIO 引脚
    .led_count = 1,          // LED 数量
    .max_brightness = 255,   // 最大亮度
};

// 初始化
ws2811_handle_t led;
ws2811_init(&config, &led);

// 设置颜色
ws2811_set_all(led, WS2811_COLOR_RED);
ws2811_refresh(led);

// 释放资源
ws2811_deinit(led);
```

### 2. 控制多个 LED

```c
ws2811_config_t config = {
    .gpio_num = 48,
    .led_count = 10,  // 10 个 LED
    .max_brightness = 255,
};

ws2811_handle_t led;
ws2811_init(&config, &led);

// 设置不同的颜色
ws2811_set_pixel(led, 0, WS2811_COLOR_RED);
ws2811_set_pixel(led, 1, WS2811_COLOR_GREEN);
ws2811_set_pixel(led, 2, WS2811_COLOR_BLUE);
ws2811_refresh(led);
```

### 3. 亮度控制

```c
// 设置亮度为 50%
ws2811_set_brightness(led, 128);
ws2811_set_all(led, WS2811_COLOR_WHITE);
ws2811_refresh(led);
```

### 4. HSV 颜色

```c
// 使用 HSV 创建颜色（色调=180, 饱和度=100%, 明度=100%）
ws2811_color_t cyan = ws2811_hsv_to_rgb(180, 100, 100);
ws2811_set_all(led, cyan);
ws2811_refresh(led);
```

## API 参考

### 初始化和释放

#### `ws2811_init()`
初始化 LED 灯带

```c
esp_err_t ws2811_init(const ws2811_config_t *config, ws2811_handle_t *handle);
```

**参数：**
- `config`: LED 配置
  - `gpio_num`: GPIO 引脚号
  - `led_count`: LED 数量
  - `max_brightness`: 最大亮度 (0-255)
- `handle`: 返回的 LED 句柄

**返回：**
- `ESP_OK`: 成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_NO_MEM`: 内存不足

#### `ws2811_deinit()`
释放 LED 灯带资源

```c
esp_err_t ws2811_deinit(ws2811_handle_t handle);
```

### LED 控制

#### `ws2811_set_pixel()`
设置单个 LED 的颜色

```c
esp_err_t ws2811_set_pixel(ws2811_handle_t handle, uint32_t index, ws2811_color_t color);
```

#### `ws2811_set_pixel_rgb()`
设置单个 LED 的颜色（RGB 分量）

```c
esp_err_t ws2811_set_pixel_rgb(ws2811_handle_t handle, uint32_t index, uint8_t r, uint8_t g, uint8_t b);
```

#### `ws2811_set_all()`
设置所有 LED 为相同颜色

```c
esp_err_t ws2811_set_all(ws2811_handle_t handle, ws2811_color_t color);
```

#### `ws2811_set_all_rgb()`
设置所有 LED 为相同颜色（RGB 分量）

```c
esp_err_t ws2811_set_all_rgb(ws2811_handle_t handle, uint8_t r, uint8_t g, uint8_t b);
```

#### `ws2811_clear()`
清除所有 LED（关闭）

```c
esp_err_t ws2811_clear(ws2811_handle_t handle);
```

#### `ws2811_refresh()`
刷新 LED 显示（将缓冲区数据发送到 LED）

```c
esp_err_t ws2811_refresh(ws2811_handle_t handle);
```

**重要：** 设置颜色后必须调用 `ws2811_refresh()` 才能看到效果！

### 亮度控制

#### `ws2811_set_brightness()`
设置全局亮度

```c
esp_err_t ws2811_set_brightness(ws2811_handle_t handle, uint8_t brightness);
```

**参数：**
- `brightness`: 亮度 (0-255)，会自动限制在 `max_brightness` 以内

#### `ws2811_get_brightness()`
获取当前亮度

```c
uint8_t ws2811_get_brightness(ws2811_handle_t handle);
```

### 颜色工具

#### `ws2811_hsv_to_rgb()`
从 HSV 转换为 RGB 颜色

```c
ws2811_color_t ws2811_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v);
```

**参数：**
- `h`: 色调 (0-360)
- `s`: 饱和度 (0-100)
- `v`: 明度 (0-100)

#### `ws2811_color()`
创建颜色

```c
ws2811_color_t ws2811_color(uint8_t r, uint8_t g, uint8_t b);
```

#### `ws2811_dim_color()`
调整颜色亮度

```c
ws2811_color_t ws2811_dim_color(ws2811_color_t color, uint8_t brightness);
```

## 预定义颜色

```c
WS2811_COLOR_RED        // 红色
WS2811_COLOR_GREEN      // 绿色
WS2811_COLOR_BLUE       // 蓝色
WS2811_COLOR_WHITE      // 白色
WS2811_COLOR_YELLOW     // 黄色
WS2811_COLOR_CYAN       // 青色
WS2811_COLOR_MAGENTA    // 洋红色
WS2811_COLOR_ORANGE     // 橙色
WS2811_COLOR_PURPLE     // 紫色
WS2811_COLOR_PINK       // 粉色
WS2811_COLOR_OFF        // 关闭（黑色）
```

## 示例代码

查看 `ws2811_example.c` 文件，包含以下示例：

1. **基本使用** - 设置单色
2. **呼吸灯效果** - 渐亮渐暗
3. **彩虹效果** - 多彩流动
4. **闪烁效果** - 快速开关
5. **追逐效果** - LED 依次点亮
6. **渐变效果** - 颜色平滑过渡
7. **状态指示灯** - 不同状态显示不同颜色

## 硬件连接

### 单个 LED

```
ESP32          WS2812
-----          ------
GPIO 48  --->  DIN
GND      --->  GND
5V       --->  VCC
```

### LED 灯带

```
ESP32          WS2812 Strip
-----          ------------
GPIO 48  --->  DIN
GND      --->  GND
5V       --->  VCC (外部供电推荐)
```

**注意：**
- 多个 LED 时建议使用外部 5V 电源供电
- 每个 LED 最大电流约 60mA（全白色最亮时）
- 添加 100-470μF 电容在电源端可以稳定供电

## 常见问题

### Q1: LED 不亮？

检查：
1. GPIO 引脚是否正确
2. 电源是否连接
3. 是否调用了 `ws2811_refresh()`
4. 亮度是否设置为 0

### Q2: 颜色不对？

WS2812 使用 GRB 格式，库已自动处理。如果颜色仍然不对，可能是：
1. LED 型号不是 WS2812（尝试修改 `led_model`）
2. 电源电压不足

### Q3: 多个 LED 时闪烁？

可能是：
1. 电源供电不足，使用外部电源
2. 数据线太长，添加上拉电阻或使用电平转换器

### Q4: 如何降低功耗？

1. 降低 `max_brightness`
2. 使用较暗的颜色
3. 减少同时点亮的 LED 数量

## 性能优化

### 减少刷新次数

```c
// 不好的做法：每次设置都刷新
for (int i = 0; i < 10; i++) {
    ws2811_set_pixel(led, i, color);
    ws2811_refresh(led);  // 刷新 10 次
}

// 好的做法：批量设置后刷新一次
for (int i = 0; i < 10; i++) {
    ws2811_set_pixel(led, i, color);
}
ws2811_refresh(led);  // 只刷新 1 次
```

### 使用合适的亮度

```c
// 全亮度（255）功耗最大
ws2811_set_brightness(led, 255);

// 50% 亮度，功耗约为 1/4
ws2811_set_brightness(led, 128);

// 25% 亮度，功耗约为 1/16
ws2811_set_brightness(led, 64);
```

## 集成到项目

### 1. 添加到 CMakeLists.txt

在 `main/CMakeLists.txt` 中添加：

```cmake
idf_component_register(
    SRCS
        # ... 其他源文件 ...
        "ws2811/ws2811.c"
    INCLUDE_DIRS
        "."
        "ws2811"
    REQUIRES
        led_strip
)
```

### 2. 在代码中使用

```c
#include "ws2811/ws2811.h"

void app_main(void) {
    ws2811_config_t config = {
        .gpio_num = 48,
        .led_count = 1,
        .max_brightness = 255,
    };

    ws2811_handle_t led;
    ws2811_init(&config, &led);

    // 你的代码...

    ws2811_deinit(led);
}
```

## 许可证

MIT License - 与项目主许可证相同

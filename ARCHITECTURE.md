# 小智 ESP32 系统架构与运行机制详解

> 本文档详细说明小智ESP32固件的完整架构、状态机系统、交互机制和AI调用流程
>
> **更新日期**: 2026-01-25
> **适用版本**: waveshare-c6-lcd-1.69

---

## 目录

- [1. 项目整体架构](#1-项目整体架构)
- [2. 程序启动与初始化流程](#2-程序启动与初始化流程)
- [3. 主事件循环](#3-主事件循环)
- [4. 设备状态机详解](#4-设备状态机详解)
- [5. 宠物状态机详解](#5-宠物状态机详解)
- [6. 触摸交互机制详解](#6-触摸交互机制详解)
- [7. 背景系统架构](#7-背景系统架构)
- [8. MCP AI调用机制详解](#8-mcp-ai调用机制详解)
- [9. 动画系统详解](#9-动画系统详解)
- [10. 通信协议层详解](#10-通信协议层详解)
- [11. 完整交互流程示例](#11-完整交互流程示例)
- [12. 关键技术要点总结](#12-关键技术要点总结)

---

## 1. 项目整体架构

```
xiaozhi-esp32 (小智AI助手ESP32固件)
│
├─ 硬件抽象层 (HAL)
│  ├─ boards/waveshare-c6-lcd-1.69/  # 开发板驱动
│  ├─ audio/codecs/                   # 音频编解码器
│  ├─ display/                        # 显示驱动
│  └─ touch/                          # 触摸驱动 (CST816S)
│
├─ 服务层
│  ├─ audio_service                   # 音频服务 (录音/播放/唤醒词)
│  ├─ protocol (MQTT/WebSocket)       # 通信协议 (与云端AI通信)
│  ├─ mcp_server                      # MCP工具服务器 (AI控制接口)
│  └─ network                         # 网络管理 (WiFi/4G)
│
├─ 状态机层
│  ├─ device_state_machine            # 设备状态机
│  ├─ pet_state_machine               # 宠物状态机
│  └─ background_manager              # 背景管理器
│
├─ 业务逻辑层
│  ├─ application                     # 主应用 (事件循环)
│  ├─ pet system (宠物系统)
│  ├─ coin system (金币系统)
│  ├─ achievement system (成就系统)
│  └─ memory system (记忆系统)
│
└─ 资源层
   ├─ gifs/                           # 动画资源 (frames.bin)
   ├─ animations (动画加载器)
   ├─ backgrounds (背景加载器)
   └─ items (物品加载器)
```

### 1.1 核心文件结构

| 目录/文件 | 说明 |
|----------|------|
| `main/application.cc` | 主应用程序，事件循环 |
| `main/device_state_machine.cc` | 设备状态机实现 |
| `main/pet/pet_state.cc` | 宠物状态机实现 |
| `main/images/background_manager.cc` | 背景管理器 |
| `main/images/animation_loader.cc` | 动画加载器 |
| `main/mcp_server.cc` | MCP服务器实现 |
| `main/protocols/mqtt_protocol.cc` | MQTT通信协议 |
| `main/protocols/websocket_protocol.cc` | WebSocket通信协议 |
| `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc` | 开发板驱动 |

---

## 2. 程序启动与初始化流程

**文件**: `main/application.cc:69-217`

```
Application::Initialize()
│
├─ 1. 设置初始状态
│     └─ SetDeviceState(kDeviceStateStarting)
│
├─ 2. 初始化显示系统
│     ├─ Board::GetDisplay()
│     ├─ 创建LVGL界面
│     ├─ 初始化动画系统
│     └─ 显示系统信息
│
├─ 3. 初始化音频服务
│     ├─ audio_service_.Initialize(codec)
│     ├─ 启动录音/播放任务
│     └─ 注册回调:
│         ├─ on_wake_word_detected  → 唤醒词触发
│         ├─ on_vad_change          → 语音活动检测
│         └─ on_send_queue_available → 音频数据可发送
│
├─ 4. 添加状态变化监听器
│     ├─ 设备状态变化 → 触发MAIN_EVENT_STATE_CHANGED
│     └─ 设备状态变化 → 同步到PetStateMachine
│
├─ 5. 初始化MCP Server
│     ├─ McpServer::GetInstance()
│     ├─ AddCommonTools() - 通用工具
│     │   ├─ self.get_device_status
│     │   ├─ self.audio_speaker.set_volume
│     │   ├─ self.screen.set_brightness
│     │   └─ self.screen.set_theme
│     ├─ RegisterMemoryMcpTools() - 记忆工具
│     ├─ RegisterPetMcpTools() - 宠物工具
│     │   ├─ pet(action='status')
│     │   ├─ pet(action='feed')
│     │   ├─ pet(action='bathe')
│     │   ├─ pet(action='play')
│     │   └─ pet(action='heal')
│     └─ RegisterBackgroundMcpTools() - 背景工具
│         ├─ background(action='status')
│         ├─ background(action='set', name='...')
│         ├─ background(action='auto')
│         └─ background(action='weather', type='...')
│
├─ 6. 初始化宠物系统
│     ├─ PetStateMachine::Initialize()
│     │   ├─ 从NVS加载宠物状态
│     │   └─ 如果宠物死亡 → 创建新宠物
│     ├─ CoinSystem::Initialize()
│     ├─ PetAchievements::Initialize()
│     └─ 监听设备状态 → 同步宠物动作
│
├─ 7. 初始化用户界面
│     ├─ 创建底部状态栏
│     ├─ 创建宠物状态显示（5个图标）
│     └─ 设置初始文本
│
├─ 8. 初始化按键/触摸
│     ├─ InitializeTouch() - CST816S触摸芯片
│     ├─ 配置中断引脚 (GPIO 11)
│     └─ 启动触摸定时器 (167ms)
│
└─ 9. 启动网络连接
      └─ Board::StartNetwork()
```

### 2.1 初始化顺序重要性

| 顺序 | 模块 | 原因 |
|------|------|------|
| 1 | Display | 需要尽早显示状态给用户 |
| 2 | Audio | 音频服务需要在网络前准备好 |
| 3 | State Machine | 状态机要在业务逻辑前初始化 |
| 4 | MCP Server | 需要在网络连接前注册工具 |
| 5 | Pet System | 需要在MCP工具注册后初始化 |
| 6 | Network | 最后启动，可能需要等待配置 |

---

## 3. 主事件循环

**文件**: `main/application.cc:219-337`

```
Application::Run() - FreeRTOS任务，无限循环
│
└─ xEventGroupWaitBits() 阻塞等待事件
    │
    ├─ MAIN_EVENT_WAKE_WORD_DETECTED (唤醒词检测)
    │   └─ HandleWakeWordDetectedEvent()
    │       ├─ TransitionTo(kDeviceStateConnecting)
    │       ├─ Protocol::OpenAudioChannel()
    │       └─ Protocol::SendWakeWordDetected()
    │
    ├─ MAIN_EVENT_TOGGLE_CHAT (按键/触摸切换)
    │   └─ HandleToggleChatEvent()
    │       ├─ Idle → Listening  (开始聆听)
    │       │   ├─ OpenAudioChannel()
    │       │   └─ SendStartListening()
    │       └─ Listening → Idle  (停止聆听)
    │           ├─ SendStopListening()
    │           └─ CloseAudioChannel()
    │
    ├─ MAIN_EVENT_SEND_AUDIO (发送音频数据)
    │   └─ while (packet = PopPacketFromSendQueue())
    │       └─ protocol_->SendAudio(packet)
    │           ├─ MQTT: AES加密 + UDP发送
    │           └─ WebSocket: Base64编码 + WS发送
    │
    ├─ MAIN_EVENT_STATE_CHANGED (设备状态变化)
    │   └─ HandleStateChangedEvent()
    │       ├─ LED指示灯更新
    │       ├─ UI状态更新
    │       └─ PetStateMachine同步
    │
    ├─ MAIN_EVENT_CLOCK_TICK (每秒定时器)
    │   ├─ UpdateStatusBar() - 更新状态栏
    │   ├─ CoinSystem::CheckRewardTimer() - 检查金币奖励
    │   ├─ 每60秒:
    │   │   ├─ PetStateMachine::Tick() - 宠物属性衰减
    │   │   ├─ CoinSystem::CheckDailyReset() - 每日重置
    │   │   └─ CoinSystem::CheckAutoConsumption() - 自动消费
    │   └─ 每5秒:
    │       └─ SetPetStatus(stats, coins) - 显示宠物状态
    │
    ├─ MAIN_EVENT_NETWORK_CONNECTED (网络连接成功)
    │   └─ HandleNetworkConnectedEvent()
    │       └─ TransitionTo(kDeviceStateActivating)
    │           └─ 启动激活任务
    │
    └─ MAIN_EVENT_SCHEDULE (调度任务执行)
        └─ 执行main_tasks_队列中的所有任务
```

### 3.1 事件优先级

虽然FreeRTOS事件组不保证处理顺序，但通过代码顺序隐含了优先级：

1. **ERROR** - 最高优先级，立即处理
2. **NETWORK_CONNECTED/DISCONNECTED** - 网络状态变化
3. **STATE_CHANGED** - 设备状态变化
4. **TOGGLE_CHAT / WAKE_WORD** - 用户交互
5. **SEND_AUDIO** - 音频数据发送
6. **CLOCK_TICK** - 定时任务（最低优先级）

---

## 4. 设备状态机详解

**文件**: `main/device_state_machine.cc`

### 4.1 状态定义

**文件**: `main/device_state.h:4-16`

```cpp
enum DeviceState {
    kDeviceStateUnknown,         // 未知状态
    kDeviceStateStarting,        // 启动中
    kDeviceStateWifiConfiguring, // WiFi配置
    kDeviceStateIdle,            // 空闲待机 ★
    kDeviceStateConnecting,      // 连接AI中
    kDeviceStateListening,       // 聆听用户 ★
    kDeviceStateSpeaking,        // AI回复 ★
    kDeviceStateUpgrading,       // 固件升级
    kDeviceStateActivating,      // 激活中
    kDeviceStateAudioTesting,    // 音频测试
    kDeviceStateFatalError       // 致命错误
};
```

### 4.2 状态转换规则

**文件**: `main/device_state_machine.cc:34-102`

```
允许的状态转换：

Unknown → Starting

Starting → WifiConfiguring / Activating

WifiConfiguring → Activating / AudioTesting
AudioTesting → WifiConfiguring

Activating → Upgrading / Idle / WifiConfiguring
Upgrading → Idle / Activating

━━━━━━━━━━━━━━━━━━━━━━━━━━
核心对话循环 ★
━━━━━━━━━━━━━━━━━━━━━━━━━━
  Idle → Connecting / Listening / Speaking
  Connecting → Idle / Listening
  Listening → Speaking / Idle
  Speaking → Listening / Idle
━━━━━━━━━━━━━━━━━━━━━━━━━━

FatalError → 无法转出（需重启）
```

### 4.3 状态机实现机制

#### 4.3.1 观察者模式

**文件**: `main/device_state_machine.cc:133-146`

```cpp
int DeviceStateMachine::AddStateChangeListener(StateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    int id = next_listener_id_++;
    listeners_.emplace_back(id, std::move(callback));
    return id;
}

void DeviceStateMachine::NotifyStateChange(DeviceState old, DeviceState new) {
    // 复制回调列表（避免死锁）
    std::vector<StateCallback> callbacks_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [id, cb] : listeners_) {
            callbacks_copy.push_back(cb);
        }
    }

    // 调用所有回调
    for (const auto& cb : callbacks_copy) {
        cb(old, new);
    }
}
```

#### 4.3.2 原子操作保证线程安全

```cpp
class DeviceStateMachine {
private:
    std::atomic<DeviceState> current_state_{kDeviceStateUnknown};

public:
    DeviceState GetState() const {
        return current_state_.load();  // 原子读取
    }

    bool TransitionTo(DeviceState new_state) {
        DeviceState old_state = current_state_.load();

        if (!IsValidTransition(old_state, new_state)) {
            return false;
        }

        current_state_.store(new_state);  // 原子写入
        NotifyStateChange(old_state, new_state);
        return true;
    }
};
```

### 4.4 状态变化监听器示例

**Application初始化时注册**：

```cpp
// 触发UI更新
state_machine_.AddStateChangeListener([this](DeviceState old, DeviceState new) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_STATE_CHANGED);
});

// 同步到宠物状态机
state_machine_.AddStateChangeListener([](DeviceState old, DeviceState new) {
    PetStateMachine::GetInstance().OnDeviceStateChanged(old, new);
});
```

---

## 5. 宠物状态机详解

**文件**: `main/pet/pet_state.cc`

### 5.1 宠物动作定义

**文件**: `main/pet/pet_state.h:9-20`

```cpp
enum class PetAction : uint8_t {
    kIdle,       // 待机 → idle 动画
    kEating,     // 吃东西 → eat 动画
    kBathing,    // 洗澡 → bath 动画
    kSleeping,   // 睡觉 → sleep 动画
    kPlaying,    // 玩耍 → walk 动画
    kSick,       // 生病 → sleep 动画
    // 与小智语音交互联动
    kListening,  // 聆听用户 → listen 动画
    kSpeaking,   // 说话中 → talk 动画
    kThinking,   // 思考中 → walk 动画
};
```

### 5.2 宠物属性系统

**文件**: `main/pet/pet_state.h:23-37`

```cpp
struct PetStats {
    int8_t hunger;        // 饱食度 0-100
    int8_t happiness;     // 快乐度 0-100
    int8_t cleanliness;   // 清洁度 0-100
    int8_t health;        // 健康值 0-100
    uint32_t age_minutes; // 年龄（分钟）
    bool is_alive;        // 是否存活
};

// 每分钟衰减配置
struct DecayConfig {
    int8_t hunger_per_min = 1;
    int8_t happiness_per_min = 1;
    int8_t cleanliness_per_min = 1;
};
```

### 5.3 设备状态→宠物动作同步

**文件**: `main/pet/pet_state.cc:193-229`

```cpp
void PetStateMachine::OnDeviceStateChanged(DeviceState old, DeviceState new) {
    switch (new_state) {
        case kDeviceStateListening:
            // 进入语音交互
            if (!in_voice_interaction_) {
                previous_pet_action_ = current_action_;  // 保存当前动作
                in_voice_interaction_ = true;
            }
            SetAction(PetAction::kListening);  // 播放 listen 动画
            break;

        case kDeviceStateSpeaking:
            SetAction(PetAction::kSpeaking);   // 播放 talk 动画
            break;

        case kDeviceStateConnecting:
            SetAction(PetAction::kThinking);   // 播放 walk 动画
            break;

        case kDeviceStateIdle:
            // 语音交互结束，恢复之前的动作
            if (in_voice_interaction_) {
                in_voice_interaction_ = false;
                if (action_duration_ > 0) {
                    SetAction(previous_pet_action_, action_duration_);  // 恢复定时动作
                } else {
                    SetAction(PetAction::kIdle);  // 回到idle
                }
            }
            break;
    }
}
```

### 5.4 宠物动作→动画映射

**文件**: `main/pet/pet_state.cc:306-323`

```cpp
const char* PetStateMachine::ActionToAnimation(PetAction action) {
    switch (action) {
        // 宠物专属动作
        case PetAction::kIdle:      return "idle";   // 待机
        case PetAction::kEating:    return "eat";    // 吃饭
        case PetAction::kBathing:   return "bath";   // 洗澡
        case PetAction::kSleeping:  return "sleep";  // 睡觉
        case PetAction::kPlaying:   return "walk";   // 玩耍=行走
        case PetAction::kSick:      return "sleep";  // 生病=睡觉

        // 与小智语音联动
        case PetAction::kListening: return "listen"; // 聆听
        case PetAction::kSpeaking:  return "talk";   // 讲话
        case PetAction::kThinking:  return "walk";   // 思考=行走

        default: return "idle";
    }
}
```

### 5.5 宠物属性自动衰减

**文件**: `main/pet/pet_state.cc:38-103`

```cpp
void PetStateMachine::Tick() {  // 每分钟调用一次
    if (!stats_.is_alive) return;

    // 1. 检查定时动作是否结束
    UpdateActionTimer();

    // 2. 属性衰减
    stats_.hunger = Clamp(stats_.hunger - decay_config_.hunger_per_min);
    stats_.happiness = Clamp(stats_.happiness - decay_config_.happiness_per_min);
    stats_.cleanliness = Clamp(stats_.cleanliness - decay_config_.cleanliness_per_min);

    // 3. 年龄增加
    stats_.age_minutes++;

    // 4. 检查健康状态
    if (stats_.hunger < 20 || stats_.cleanliness < 20 || stats_.happiness < 20) {
        stats_.health = Clamp(stats_.health - 2);  // 属性过低 → 健康下降
    }

    // 5. 心情与属性联动
    if (stats_.happiness > 80) {
        happy_coin_timer_++;
        if (happy_coin_timer_ >= 60) {
            CoinSystem::GetInstance().AddCoins(1);  // 每小时+1金币
            happy_coin_timer_ = 0;
        }
    }

    if (stats_.hunger >= 100 && stats_.cleanliness >= 100) {
        stats_.happiness = 100;  // 满足时心情满
    }

    if (stats_.hunger < 50 || stats_.cleanliness < 50) {
        low_stat_timer_++;
        if (low_stat_timer_ >= 30) {
            stats_.happiness = Clamp(stats_.happiness - 1);  // 每30分钟-1心情
            low_stat_timer_ = 0;
        }
    }

    // 6. 检查死亡条件
    if (stats_.hunger == 0 || stats_.health == 0) {
        stats_.is_alive = false;
        ESP_LOGW(TAG, "Pet has died!");
    }

    // 7. 保存到NVS
    Save();
}
```

### 5.6 宠物与用户交互

| 动作 | 效果 | 持续时间 | 动画 |
|------|------|---------|------|
| Feed() | hunger+20, happiness+5 | 5分钟 | eat |
| Bathe() | cleanliness+30, happiness+5 | 5分钟 | bath |
| Play() | happiness+15 | 5秒 | walk |
| Sleep() | health+10 | 10秒 | sleep |
| Heal() | health+20 | 即时 | - |

### 5.7 对话结束奖励

**文件**: `main/pet/pet_state.cc:169-191`

```cpp
void PetStateMachine::OnConversationEnd() {
    uint32_t rand = esp_random() % 100;

    if (rand < 30) {
        // 30% 概率: 自动吃东西
        stats_.hunger = Clamp(stats_.hunger + 10);
        stats_.happiness = Clamp(stats_.happiness + 3);
        ESP_LOGI(TAG, "Conversation → auto eat (+10 hunger, +3 happiness)");
    }
    else if (rand < 60) {
        // 30% 概率: 自动玩耍
        stats_.happiness = Clamp(stats_.happiness + 8);
        ESP_LOGI(TAG, "Conversation → auto play (+8 happiness)");
    }
    else {
        // 40% 概率: 基础快乐
        stats_.happiness = Clamp(stats_.happiness + 2);
        ESP_LOGI(TAG, "Conversation → base happiness (+2)");
    }

    PetAchievements::GetInstance().OnConversation();
    Save();
}
```

---

## 6. 触摸交互机制详解

**文件**: `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc`

### 6.1 触摸硬件配置

```
CST816S 触摸芯片配置:
├─ I2C总线
│  ├─ SDA: GPIO 7
│  ├─ SCL: GPIO 8
│  ├─ 频率: 400kHz
│  └─ 地址: 0x15
│
├─ 中断引脚
│  ├─ INT: GPIO 11
│  ├─ 模式: 输入 + 下拉
│  └─ 触发: 下降沿
│
└─ 分辨率
   └─ 280×240 像素
```

### 6.2 触摸检测流程

**位置**: 约1422-1509行

```cpp
process_touch_swipe() - 167ms定时器调用
│
├─ 1. 检查初始化状态
│     if (!touch_state.initialized || touch_state.handle == nullptr)
│         → 打印警告并返回
│
├─ 2. 读取触摸数据
│     esp_lcd_touch_read_data(touch_state.handle)
│
├─ 3. 判断触摸事件
│     ├─ 触摸按下:
│     │   ├─ 记录 start_x, start_y
│     │   ├─ 记录 start_time
│     │   └─ pressed = true
│     │
│     └─ 触摸释放:
│         ├─ 计算 distance = sqrt((x-start_x)² + (y-start_y)²)
│         ├─ 计算 duration_ms = now - start_time
│         │
│         ├─ 判断交互类型:
│         │   ├─ distance < TAP_MAX_DISTANCE (50px)
│         │   │   && duration < TAP_MAX_DURATION (600ms)
│         │   │   → 轻触 (Tap)
│         │   │   └─ handle_tap()
│         │   │       └─ Application::ToggleChatState()
│         │   │
│         │   └─ distance >= SWIPE_MIN_DISTANCE (60px)
│         │       → 滑动 (Swipe)
│         │       └─ handle_swipe()
│         │           └─ 播放 pet_head 动画
│         │
│         └─ pressed = false
```

### 6.3 触摸参数配置

**位置**: 约215行

```cpp
// 可调整的触摸检测参数
static constexpr int16_t TAP_MAX_DISTANCE = 50;      // 点击最大移动距离 (像素)
static constexpr int16_t SWIPE_MIN_DISTANCE = 60;    // 滑动最小距离 (像素)
static constexpr int64_t TAP_MAX_DURATION_MS = 600;  // 点击最大时长 (毫秒)
```

### 6.4 触摸触发的动作

#### 6.4.1 轻触 (Tap)

```cpp
handle_tap()
  └─ Application::ToggleChatState()
      │
      ├─ 当前状态 = Idle:
      │   ├─ TransitionTo(kDeviceStateListening)
      │   ├─ Protocol::OpenAudioChannel()
      │   ├─ Protocol::SendStartListening()
      │   └─ 开始录音
      │
      └─ 当前状态 = Listening:
          ├─ Protocol::SendStopListening()
          ├─ Protocol::CloseAudioChannel()
          ├─ TransitionTo(kDeviceStateIdle)
          └─ 停止录音
```

#### 6.4.2 滑动 (Swipe)

```cpp
handle_swipe()
  └─ StartPetHeadAnimation()
      ├─ SwitchAnimation("pet_head", loop=true, interrupt=false)
      ├─ 播放13帧摸头动画 (帧26-38)
      ├─ swipe_animation_active = true
      └─ 动画持续播放直到释放
```

### 6.5 触摸休眠机制

**CST816S 触摸芯片特性**：

```
正常情况：
  - 有触摸时，芯片活跃，I2C通信正常
  - 无触摸时，芯片进入休眠节省电力
  - 休眠状态下，I2C读取会返回NACK错误

日志示例（正常现象）：
  E (18149) i2c.master: I2C transaction unexpected nack detected
  E (18159) lcd_panel.io.i2c: panel_io_i2c_rx_buffer(145): i2c transaction failed
  E (18159) CST816S: esp_lcd_touch_cst816s_read_data(114): I2C read failed

判断是否正常：
  ✓ 错误间隔约167ms（定时器周期）
  ✓ 触摸时错误消失
  ✗ 初始化时就报错 → 硬件故障
  ✗ 触摸时仍报错 → 硬件故障
```

### 6.6 触摸日志级别控制

**位置**: 约2263行

```cpp
// 初始化前：临时打开日志诊断初始化问题
esp_log_level_set("i2c.master", ESP_LOG_WARN);
esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_WARN);
esp_log_level_set("CST816S", ESP_LOG_INFO);

InitializeTouch();

// 初始化后：降低日志级别避免刷屏
// (CST816S休眠时的NACK错误是正常现象)
esp_log_level_set("i2c.master", ESP_LOG_ERROR);
esp_log_level_set("lcd_panel.io.i2c", ESP_LOG_ERROR);
esp_log_level_set("CST816S", ESP_LOG_WARN);
```

---

## 7. 背景系统架构

**文件**: `main/images/background_manager.cc`

### 7.1 背景类型定义

**文件**: `main/images/background_manager.h:9-35`

```cpp
enum BackgroundIndex {
    // 时间背景 (0-3) - 始终可用
    BG_TIME_DAY = 0,        // 白天 (08:00-16:59)
    BG_TIME_SUNSET = 1,     // 夕阳 (17:00-18:59)
    BG_TIME_SUNRISE = 2,    // 日出 (05:00-07:59)
    BG_TIME_NIGHT = 3,      // 黑夜 (19:00-04:59)

    // 天气背景 (4) - MCP控制
    BG_WEATHER_RAINY = 4,   // 雨天

    // 节日背景 (5-11) - 需解锁
    BG_FESTIVAL_CHRISTMAS = 5,   // 圣诞 (12/24-25)
    BG_FESTIVAL_BIRTHDAY = 6,    // 生日 (用户设置)
    BG_FESTIVAL_SPRING = 7,      // 春节 (1/22-2/5)
    BG_FESTIVAL_NEWYEAR = 8,     // 元旦 (1/1)
    BG_FESTIVAL_MIDAUTUMN = 9,   // 中秋 (9/10-20)
    BG_FESTIVAL_HALLOWEEN = 10,  // 万圣节 (10/31)
    BG_FESTIVAL_VALENTINES = 11, // 情人节 (2/14)

    // 风格背景 (12-15) - 需解锁
    BG_STYLE_CYBERPUNK = 12,  // 赛博朋克
    BG_STYLE_STEAMPUNK = 13,  // 蒸汽朋克
    BG_STYLE_FANTASY = 14,    // 奇幻
    BG_STYLE_SPACE = 15,      // 太空舱

    BG_COUNT = 16
};
```

### 7.2 背景选择优先级（2026-01-25修正版）

**文件**: `main/images/background_manager.cc:73-127`

```
GetCurrentBackground() - 返回背景索引 (0-15)
│
├─ 优先级1: Force Mode (MCP强制指定)
│   └─ if (force_enabled_)
│       └─ return forced_background_
│           └─ 通过 MCP background(action='set', name='xxx') 设置
│
├─ 优先级2: 节日背景 (全天显示)
│   └─ CheckFestival(bg_idx)
│       ├─ 检查日期是否匹配节日
│       ├─ 检查节日是否已解锁 (PetAchievements)
│       └─ if (匹配 && 已解锁)
│           └─ return 节日背景（整天保持不变）
│
├─ 优先级3: 天气背景 (独立背景)
│   └─ CheckWeather(bg_idx)
│       └─ if (current_weather_ == WEATHER_RAINY)
│           └─ return BG_WEATHER_RAINY
│               └─ 替换所有背景（包括时间/风格）
│
└─ 优先级4: 时间段背景 (时间或风格) ★ 关键逻辑
    │
    ├─ 检测时间段切换:
    │   └─ current_period = GetTimePeriod()  // 返回 0-3
    │       ├─ 0: 05:00-07:59 (sunrise)
    │       ├─ 1: 08:00-16:59 (day)
    │       ├─ 2: 17:00-18:59 (sunset)
    │       └─ 3: 19:00-04:59 (night)
    │
    ├─ if (current_period != last_time_period_)  ★ 时间段改变
    │   ├─ last_time_period_ = current_period
    │   ├─ bg_decided_this_period_ = false
    │   │
    │   ├─ CheckSpecialRandom(bg_idx):  // 20%概率触发
    │   │   ├─ 获取已解锁的风格背景列表
    │   │   ├─ if (unlocked_styles.empty())
    │   │   │   └─ return false
    │   │   ├─ rand = esp_random() % 100
    │   │   └─ if (rand < 20)  // 20%概率
    │   │       ├─ bg_idx = random_select(unlocked_styles)
    │   │       └─ return true
    │   │
    │   ├─ if (CheckSpecialRandom成功)  // 20%情况
    │   │   ├─ current_decided_bg_ = bg_idx (风格背景)
    │   │   ├─ bg_decided_this_period_ = true
    │   │   └─ return bg_idx  // 返回风格背景
    │   │
    │   └─ else  // 80%情况
    │       ├─ time_bg = GetTimeBackground()  // 时间背景
    │       ├─ current_decided_bg_ = time_bg
    │       ├─ bg_decided_this_period_ = true
    │       └─ return time_bg  // 返回时间背景
    │
    └─ else  ★ 时间段未变化
        └─ return current_decided_bg_  // 保持当前背景
```

### 7.3 关键修改说明（2026-01-25）

#### 7.3.1 修改前的问题

```cpp
// ❌ 错误：每次调用GetCurrentBackground()都触发20%随机
uint16_t BackgroundManager::GetCurrentBackground() {
    // 每次都检查20%概率 → 背景不稳定
    if (CheckSpecialRandom(bg_idx)) {
        return bg_idx;
    }
    // ...
}

问题：
  1. 每次GetCurrentBackground()都触发随机
  2. 背景可能每秒都在变化
  3. 用户体验差
```

#### 7.3.2 修改后的正确逻辑

```cpp
// ✅ 正确：只在时间段切换时触发20%随机
uint16_t BackgroundManager::GetCurrentBackground() {
    // 检测时间段是否改变
    uint8_t current_period = GetTimePeriod();

    if (current_period != last_time_period_) {  // 只在时间段切换时触发
        last_time_period_ = current_period;

        if (CheckSpecialRandom(bg_idx)) {
            // 20%概率选择风格背景
            current_decided_bg_ = bg_idx;
            return bg_idx;
        } else {
            // 80%概率选择时间背景
            uint16_t time_bg = GetTimeBackground();
            current_decided_bg_ = time_bg;
            return time_bg;
        }
    }

    // 时间段未变化，保持当前背景
    return current_decided_bg_;
}

优点：
  1. 背景稳定，在同一时间段内不变
  2. 只在特定时间点切换 (5:00, 8:00, 17:00, 19:00)
  3. 用户体验好
```

#### 7.3.3 新增的状态变量

```cpp
class BackgroundManager {
private:
    uint8_t last_time_period_;        // 上次的时间段 (0-3)
    uint16_t current_decided_bg_;     // 当前决定的背景索引
    bool bg_decided_this_period_;     // 本时间段是否已决定背景
};
```

### 7.4 背景切换时间点

```
时间段边界（触发20%随机的时刻）:
┌────────────────────────────────────────────────────┐
│  05:00  sunrise → day        可能切换背景          │
│  08:00  day → day            可能切换背景          │
│  17:00  day → sunset         可能切换背景          │
│  19:00  sunset → night       可能切换背景          │
└────────────────────────────────────────────────────┘

非时间段边界（保持当前背景）:
┌────────────────────────────────────────────────────┐
│  05:01 ~ 07:59  保持05:00时决定的背景              │
│  08:01 ~ 16:59  保持08:00时决定的背景              │
│  17:01 ~ 18:59  保持17:00时决定的背景              │
│  19:01 ~ 04:59  保持19:00时决定的背景              │
└────────────────────────────────────────────────────┘

示例：
  08:00 → GetCurrentBackground()
    → 检测到时间段切换 (night → day)
    → 触发20%随机
    → 假设随机到 cyberpunk 风格背景
    → current_decided_bg_ = BG_STYLE_CYBERPUNK
    → 返回 12 (cyberpunk)

  08:30 → GetCurrentBackground()
    → 时间段未变化 (仍是 day)
    → 直接返回 current_decided_bg_ (12)
    → 背景保持 cyberpunk

  16:59 → GetCurrentBackground()
    → 时间段未变化 (仍是 day)
    → 直接返回 current_decided_bg_ (12)
    → 背景保持 cyberpunk

  17:00 → GetCurrentBackground()
    → 检测到时间段切换 (day → sunset)
    → 触发20%随机
    → 假设未触发（80%情况）
    → current_decided_bg_ = BG_TIME_SUNSET (1)
    → 返回 1 (sunset)
    → 背景切换为夕阳
```

### 7.5 背景解锁机制

**文件**: `main/pet/pet_achievements.h`

```cpp
class PetAchievements {
public:
    // 节日背景解锁方法
    bool IsChristmasUnlocked() const;
    bool IsBirthdayUnlocked() const;
    bool IsSpringFestivalUnlocked() const;
    bool IsNewYearUnlocked() const;
    bool IsMidAutumnUnlocked() const;
    bool IsHalloweenUnlocked() const;
    bool IsValentinesUnlocked() const;

    // 风格背景解锁方法
    bool IsCyberpunkUnlocked() const;
    bool IsSteampunkUnlocked() const;
    bool IsFantasyUnlocked() const;
    bool IsSpaceUnlocked() const;
};
```

**解锁条件**：

```
初始状态:
  ✓ 4个时间背景（始终可用）
  ✗ 1个天气背景（需MCP设置）
  ✗ 7个节日背景（需解锁）
  ✗ 4个风格背景（需解锁）

解锁途径:
  1. 拾取场景金币时 1% 概率解锁随机背景
  2. 金币≥10 时，通过MCP对话控制解锁
  3. 达成特定成就时自动解锁
```

---

## 8. MCP AI调用机制详解

### 8.1 MCP Server架构

**文件**: `main/mcp_server.h:319-347`

```cpp
class McpServer {
public:
    static McpServer& GetInstance();  // 单例模式

    // 工具管理
    void AddTool(name, description, properties, callback);
    void AddUserOnlyTool(...);  // 用户专用工具（AI不可见）

    // 消息处理
    void ParseMessage(const std::string& message);  // 解析JSON-RPC请求
    void DoToolCall(int id, const std::string& tool_name, const cJSON* args);

private:
    std::vector<McpTool*> tools_;  // 工具列表
};
```

### 8.2 MCP工具注册

#### 8.2.1 通用工具

**文件**: `main/mcp_server.cc:33-126`

```cpp
McpServer::AddCommonTools()
  │
  ├─ self.get_device_status
  │   └─ 获取设备状态（音量、亮度、电量、网络等）
  │
  ├─ self.audio_speaker.set_volume
  │   └─ 设置音量 (0-100)
  │
  ├─ self.screen.set_brightness
  │   └─ 设置亮度 (0-100)
  │
  ├─ self.screen.set_theme
  │   └─ 设置主题 (light/dark)
  │
  └─ self.camera.take_photo
      └─ 拍照并识别（需要摄像头支持）
```

#### 8.2.2 背景工具

**文件**: `main/images/background_mcp_tools.cc:162-218`

```cpp
RegisterBackgroundMcpTools(mcp_server)
  │
  └─ background  # 背景管理工具
      │
      ├─ action='status'
      │   └─ 查询当前背景和可用背景列表
      │       ├─ current_background: 当前背景名称
      │       ├─ current_index: 当前背景索引
      │       └─ available_backgrounds: 分类背景列表
      │           ├─ time: [day, sunset, sunrise, night]
      │           ├─ weather: [rainy]
      │           └─ style: [已解锁的风格背景]
      │
      ├─ action='set' (需name参数)
      │   └─ 切换到指定背景
      │       └─ name: day/sunset/sunrise/night/rainy/
      │                cyberpunk/steampunk/fantasy/space
      │
      ├─ action='auto'
      │   └─ 恢复自动背景模式
      │       └─ ClearForce() → force_enabled_ = false
      │
      └─ action='weather' (需type参数)
          └─ 设置天气条件
              └─ type: clear/rainy
```

#### 8.2.3 宠物工具

**文件**: `main/pet/pet_mcp_tools.cc`

```cpp
RegisterPetMcpTools(mcp_server)
  │
  └─ pet  # 宠物交互工具
      │
      ├─ action='status'
      │   └─ 查询宠物状态
      │       └─ 返回: {hunger, happiness, cleanliness,
      │                health, age_days, coins, mood, is_alive}
      │
      ├─ action='feed'
      │   └─ 喂食宠物
      │       └─ hunger+20, happiness+5, 播放eat动画5分钟
      │
      ├─ action='bathe'
      │   └─ 给宠物洗澡
      │       └─ cleanliness+30, happiness+5, 播放bath动画5分钟
      │           清除所有便便
      │
      ├─ action='play'
      │   └─ 和宠物玩耍
      │       └─ happiness+15, 播放walk动画5秒
      │
      └─ action='heal'
          └─ 治疗宠物
              └─ health+20
```

### 8.3 AI调用工具完整流程

```
┌─────────────────────────────────────────────────────────┐
│  用户语音 → AI理解 → 工具调用 → 执行 → 返回结果        │
└─────────────────────────────────────────────────────────┘

1. 用户说话
   "切换到夜晚背景"

2. 音频发送到云端
   (录音 → AES加密 → UDP/WebSocket发送)

3. AI语音识别
   文本: "切换到夜晚背景"

4. AI理解意图
   ├─ 识别关键词: "切换" + "夜晚" + "背景"
   ├─ 确定目标: 更改背景
   └─ 决定调用: background工具

5. AI生成工具调用请求
   {
     "jsonrpc": "2.0",
     "method": "tools/call",
     "params": {
       "name": "background",
       "arguments": {
         "action": "set",
         "name": "night"
       }
     },
     "id": 123
   }

6. 发送到设备
   Protocol::SendText(json_string)
   └─ MQTT: 发布到 topic "/push/{device_id}"
      WebSocket: 通过WS连接发送

7. 设备接收消息
   Protocol::OnTextMessage(json_string)
   └─ 触发回调 → 传递给MCP Server

8. MCP Server解析请求
   McpServer::ParseMessage(json_string)
   ├─ 解析JSON-RPC格式
   ├─ 提取method: "tools/call"
   ├─ 提取tool_name: "background"
   ├─ 提取arguments: {action: "set", name: "night"}
   └─ 路由到 DoToolCall()

9. 执行工具调用
   McpServer::DoToolCall(123, "background", arguments)
   ├─ 查找工具: tools_.find("background")
   ├─ 解析参数:
   │   ├─ action = arguments["action"] = "set"
   │   └─ name = arguments["name"] = "night"
   └─ 调用工具回调函数

10. Background工具回调执行
    HandleSetBackground("night")
    ├─ 查找背景映射:
    │   for (kBackgroundMap)
    │     if (name == "night")
    │       → BG_TIME_NIGHT (index=3)
    │
    ├─ 检查解锁状态:
    │   if (category == "style")
    │     → 检查 achievements.IsXxxUnlocked()
    │   if (category == "time")
    │     → 始终可用 ✓
    │
    └─ 强制设置背景:
        BackgroundManager::ForceBackground(3)
        ├─ force_enabled_ = true
        └─ forced_background_ = 3

11. 背景立即更新
    GetCurrentBackground()
    └─ 优先级1: force_enabled_ = true
        → return forced_background_ (3)

12. 加载新背景
    BackgroundLoader::LoadBackground(3)
    ├─ 计算Flash偏移:
    │   offset = 2,742,272 + (3 × 67,968) = 2,946,176
    │
    ├─ 读取背景数据:
    │   esp_partition_read(partition, offset, buffer, 67968)
    │   280×240 × 3字节RGB888 = 201,600字节
    │   + 256色调色板 = 67,968字节总共
    │
    └─ 解码为RGB565:
        for (每个像素)
            rgb565 = RGB888_TO_RGB565(rgb888)

13. 显示更新
    ├─ 更新LVGL背景图片对象
    │   lv_img_set_src(bg_img, rgb565_buffer)
    └─ 刷新显示
        → 屏幕显示夜晚背景

14. 返回结果给AI
    McpServer::ReplyResult(123, "Switched to background: night")
    {
      "jsonrpc": "2.0",
      "result": {
        "content": [{
          "type": "text",
          "text": "Switched to background: night"
        }],
        "isError": false
      },
      "id": 123
    }

15. 发送回AI
    Protocol::SendText(result_json)
    └─ MQTT/WebSocket 发送

16. AI接收工具调用结果
    └─ 解析result.content[0].text
        → "Switched to background: night"

17. AI生成自然语言回复
    "好的，已经切换到夜晚背景了，星空很美吧~"

18. 语音合成并播放
    └─ 用户听到AI的语音回复
```

### 8.4 MCP工具调用示例

#### 示例1: 查询宠物状态

```json
// 用户: "我的宠物怎么样了？"

// AI调用
pet(action='status')

// 返回结果
{
  "hunger": 65,
  "happiness": 80,
  "cleanliness": 45,
  "health": 90,
  "age_days": 3,
  "coins": 12,
  "mood": "状态正常",
  "is_alive": true
}

// AI回复
"你的宠物现在饱食度65，心情很不错(80)，不过有点脏了(45)，
健康值90。已经3天大了，攒了12个金币呢~ 要不要给它洗个澡？"
```

#### 示例2: 设置天气

```json
// 用户: "外面下雨了"

// AI调用
background(action='weather', type='rainy')

// 执行流程
BackgroundManager::UpdateWeather(WEATHER_RAINY)
  → current_weather_ = WEATHER_RAINY

GetCurrentBackground()
  → 优先级3: CheckWeather()
    → return BG_WEATHER_RAINY (4)

// 返回结果
"Weather set to: rainy"

// AI回复
"好的，我已经把背景换成雨天模式了，记得带伞哦~"
```

#### 示例3: 喂食宠物

```json
// 用户: "喂小狗吃点东西"

// AI调用
pet(action='feed')

// 执行流程
PetStateMachine::Feed()
  ├─ hunger = Clamp(65 + 20) = 85
  ├─ happiness = Clamp(80 + 5) = 85
  ├─ SetAction(kEating, 5*60*1000)
  │   └─ ActionToAnimation(kEating) → "eat"
  │       └─ 播放eat动画，持续5分钟
  └─ Save() → 保存到NVS

// 返回结果
"Fed the pet successfully. Hunger is now 85."

// AI回复
"好的，已经喂饱它了~ 它现在饱食度85，开心地吃着呢！"
```

---

## 9. 动画系统详解

### 9.1 动画定义表

**文件**: `main/images/animation_loader.cc:11-21`

```cpp
static const AnimationDef ANIMATION_TABLE[8] = {
    // name        start count fps loop
    {"idle",         0,   13, 15, true},  // 帧0-12   待机
    {"talk",        13,   13, 15, true},  // 帧13-25  讲话
    {"pet_head",    26,   13, 15, true},  // 帧26-38  摸头
    {"walk",        39,   13, 15, true},  // 帧39-51  行走
    {"listen",      52,   13, 15, true},  // 帧52-64  聆听
    {"eat",         65,   13, 15, true},  // 帧65-77  吃饭
    {"sleep",       78,   13, 15, true},  // 帧78-90  睡觉
    {"bath",        91,   13, 15, true},  // 帧91-103 洗澡
};

// 总共: 8个动画 × 13帧 = 104帧
// 每帧: 768字节调色板 + 25600字节像素 = 26368字节
// 总大小: 104 × 26368 = 2,742,272 字节 (约2.6MB)
```

### 9.2 帧格式详解

```
每帧结构 (26368字节):
┌──────────────────────────────────────┐
│ 调色板 (768字节)                     │
│   256色 × 3字节RGB888 = 768字节      │
│   格式: [R0,G0,B0, R1,G1,B1, ...]    │
├──────────────────────────────────────┤
│ 像素索引 (25600字节)                 │
│   160×160 × 1字节索引 = 25600字节    │
│   每个像素: 0-255 (索引到调色板)     │
└──────────────────────────────────────┘

Flash存储位置:
  分区: "assets" (0x800000)
  格式: 无文件头，直接连续存储帧数据

  帧0:   0x800000 + 0×26368    = 0x800000
  帧1:   0x800000 + 1×26368    = 0x806710
  帧2:   0x800000 + 2×26368    = 0x80CE20
  ...
  帧103: 0x800000 + 103×26368  = 0xA9C710

读取方式:
  esp_partition_read(partition, offset, buffer, size)
  避免ESP32-C6的mmap地址空间限制
```

### 9.3 动画加载流程

**文件**: `main/images/animation_loader.cc:99-end`

```cpp
AnimationLoader::ReadAndDecodeFrame(frame_idx)
  │
  ├─ 1. 计算Flash偏移
  │     offset = frame_idx × ANIM_FRAME_SIZE_RAW
  │            = frame_idx × 26368
  │
  ├─ 2. 读取调色板 (768字节 RGB888)
  │     esp_partition_read(partition, offset, palette_rgb888, 768)
  │
  │     数据格式: [R0,G0,B0, R1,G1,B1, ..., R255,G255,B255]
  │
  ├─ 3. 转换为RGB565调色板 (512字节)
  │     for (i = 0; i < 256; i++) {
  │         uint8_t r = palette_rgb888[i*3+0];
  │         uint8_t g = palette_rgb888[i*3+1];
  │         uint8_t b = palette_rgb888[i*3+2];
  │         palette_[i] = RGB888_TO_RGB565(r, g, b);
  │                     = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
  │     }
  │
  ├─ 4. 读取像素索引 (25600字节)
  │     esp_partition_read(partition, offset+768, pixel_buffer_, 25600)
  │
  │     数据格式: [idx0, idx1, idx2, ..., idx25599]
  │     每个idx: 0-255 (调色板索引)
  │
  └─ 5. 解码为目标格式 (根据需求)
        ├─ DecodeFrame(out_buf)
        │   └─ RGB565 (51200字节)
        │       for (i = 0; i < 25600; i++)
        │           out_buf[i] = palette_[pixel_buffer_[i]];
        │
        ├─ DecodeFrameARGB(out_buf)
        │   └─ ARGB8888 (102400字节)
        │       for (i = 0; i < 25600; i++)
        │           rgb565 = palette_[pixel_buffer_[i]];
        │           if (is_background_color(rgb565))
        │               out_buf[i] = 0x00000000;  // 透明
        │           else
        │               out_buf[i] = RGB565_TO_ARGB8888(rgb565);
        │
        └─ DecodeFrameRGB565A8(out_buf)
            └─ RGB565+Alpha (76800字节)
                for (i = 0; i < 25600; i++)
                    rgb565 = palette_[pixel_buffer_[i]];
                    out_buf[i*3+0] = (rgb565 >> 8) & 0xFF;  // RGB565高字节
                    out_buf[i*3+1] = rgb565 & 0xFF;         // RGB565低字节
                    if (is_background_color(rgb565))
                        out_buf[i*3+2] = 0x00;  // Alpha = 0 (透明)
                    else
                        out_buf[i*3+2] = 0xFF;  // Alpha = 255 (不透明)
```

### 9.4 动画播放定时器

**文件**: `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc`

```cpp
update_animation_frame() - 167ms定时器回调 (约6fps)
  │
  ├─ 1. 获取当前动画
  │     current_type = anim_mgr.current_type  // 例如: "talk"
  │     animation = GetAnimationDef(current_type)
  │     // {"talk", start=13, count=13, fps=15, loop=true}
  │
  ├─ 2. 递增帧索引
  │     anim_mgr.current_frame++
  │     if (current_frame >= animation.count) {
  │         if (animation.loop)
  │             current_frame = 0  // 循环播放
  │         else
  │             停止动画  // 一次性动画
  │     }
  │
  ├─ 3. 计算绝对帧号
  │     absolute_frame = animation.start + current_frame
  │
  │     例如: talk动画, frame=5
  │     absolute_frame = 13 + 5 = 18
  │     → 读取Flash中的第18帧数据
  │
  ├─ 4. 加载并解码帧
  │     AnimationLoader::ReadAndDecodeFrame(absolute_frame)
  │     DecodeFrameRGB565A8() → RGB565+Alpha通道
  │
  │     返回: 160×160 像素数据 (RGB565+Alpha)
  │
  ├─ 5. 与背景合成
  │     for (每个像素 i) {
  │         if (frame_alpha[i] > threshold) {
  │             // 不透明，显示动画像素
  │             final_buffer[i] = frame_rgb565[i];
  │         } else {
  │             // 透明，显示背景像素
  │             final_buffer[i] = background_rgb565[i];
  │         }
  │     }
  │
  └─ 6. 刷新显示
        lv_img_set_src(anim_mgr.bg_image, final_buffer)
        → LVGL更新显示缓冲区
        → 屏幕刷新
```

### 9.5 背景透明判断

**文件**: `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc:86-94`

```cpp
// 背景色范围宏定义 (RGB565)
#define BG_R_MIN  0    // R通道最小值
#define BG_R_MAX  1    // R通道最大值
#define BG_G_MIN  21   // G通道最小值
#define BG_G_MAX  22   // G通道最大值
#define BG_B_MIN  13   // B通道最小值
#define BG_B_MAX  13   // B通道最大值

// 判断是否为背景色
inline bool is_background_color(uint16_t rgb565) {
    uint8_t r = (rgb565 >> 11) & 0x1F;  // 5 bits
    uint8_t g = (rgb565 >> 5) & 0x3F;   // 6 bits
    uint8_t b = rgb565 & 0x1F;          // 5 bits

    return (r >= BG_R_MIN && r <= BG_R_MAX &&
            g >= BG_G_MIN && g <= BG_G_MAX &&
            b >= BG_B_MIN && b <= BG_B_MAX);
}

// 应用：
if (is_background_color(pixel_rgb565)) {
    alpha = 0x00;  // 透明
} else {
    alpha = 0xFF;  // 不透明
}
```

### 9.6 动画切换机制

**文件**: `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc`

```cpp
SwitchAnimation(animation_name, loop, interrupt)
  │
  ├─ 1. 停止当前动画定时器
  │     esp_timer_stop(anim_mgr.timer)
  │
  ├─ 2. 查找动画定义
  │     animation = AnimationLoader::GetAnimationByName(animation_name)
  │     // 例如: "talk" → {"talk", start=13, count=13, fps=15, loop=true}
  │
  ├─ 3. 更新动画状态
  │     anim_mgr.current_type = animation_type
  │     anim_mgr.current_frame = 0  // 从第一帧开始
  │
  ├─ 4. 加载第一帧
  │     UpdateAnimationFrame()
  │     → 立即显示新动画的第一帧
  │
  └─ 5. 启动定时器
        esp_timer_start_periodic(anim_mgr.timer, 167000)  // 167ms
        → 开始播放动画
```

---

## 10. 通信协议层详解

### 10.1 Protocol抽象接口

**文件**: `main/protocols/protocol.h:44-73`

```cpp
class Protocol {
public:
    // 生命周期
    virtual bool Start() = 0;

    // 音频通道管理
    virtual bool OpenAudioChannel() = 0;
    virtual void CloseAudioChannel() = 0;
    virtual bool IsAudioChannelOpened() const = 0;
    virtual bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) = 0;

    // 消息发送
    virtual bool SendText(const std::string& text);
    virtual void SendWakeWordDetected(const std::string& wake_word);
    virtual void SendStartListening(ListeningMode mode);
    virtual void SendStopListening();

    // 回调注册
    void OnConnected(std::function<void()> callback);
    void OnNetworkError(std::function<void(std::string)> callback);
    void OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket>)> callback);
    void OnTextMessage(std::function<void(std::string)> callback);
    void OnAudioChannelOpened(std::function<void()> callback);
    void OnAudioChannelClosed(std::function<void()> callback);

    // 访问器
    int server_sample_rate() const;
};
```

### 10.2 MQTT协议实现

**文件**: `main/protocols/mqtt_protocol.cc`

#### 10.2.1 连接流程

```cpp
MqttProtocol::Start()
  │
  ├─ 1. 连接MQTT Broker
  │     └─ mqtt_->Connect(host, port, client_id, user, pass)
  │
  ├─ 2. 订阅topic
  │     └─ mqtt_->Subscribe("/push/{device_id}")
  │
  └─ 3. 发送Hello消息
        └─ SendText(GetHelloMessage())
            → 请求服务器配置（UDP地址、AES密钥等）
```

#### 10.2.2 接收消息流程

```cpp
OnMqttMessage(topic, data)
  │
  ├─ 1. 解析JSON消息
  │     root = cJSON_Parse(data)
  │     message_type = root["type"]
  │
  ├─ 2. 根据类型路由
  │     ├─ "server_hello" → ParseServerHello(root)
  │     │   ├─ 提取UDP服务器地址/端口
  │     │   ├─ 提取AES密钥和nonce
  │     │   └─ 建立UDP连接
  │     │
  │     ├─ "audio" → OnIncomingAudio()
  │     │   └─ 解码音频数据
  │     │       └─ 传递给AudioService播放
  │     │
  │     ├─ "text" → OnTextMessage()
  │     │   └─ 传递给McpServer
  │     │       └─ McpServer::ParseMessage()
  │     │
  │     └─ "tool_result" → MCP工具调用结果
  │         └─ 传递给McpServer处理
  │
  └─ 3. 触发对应回调
```

#### 10.2.3 发送音频流程

```cpp
SendAudio(packet)
  │
  ├─ 1. AES-CTR加密
  │     └─ mbedtls_aes_crypt_ctr(
  │           &aes_ctx_,
  │           packet->data, packet->size,
  │           encrypted_data
  │         )
  │
  ├─ 2. 添加序列号头部
  │     header = {
  │       sequence: local_sequence_++,  // 防重放
  │       timestamp: current_time
  │     }
  │
  └─ 3. UDP发送
        udp_->Send(udp_server_, udp_port_,
                   [header + encrypted_data])
```

### 10.3 WebSocket协议实现

**文件**: `main/protocols/websocket_protocol.cc`

#### 10.3.1 连接流程

```cpp
WebsocketProtocol::Start()
  │
  ├─ 1. 建立WebSocket连接
  │     websocket_->Connect("wss://api.example.com/v1/ws")
  │
  └─ 2. 发送Hello消息
        SendText(GetHelloMessage())
```

#### 10.3.2 接收消息流程

```cpp
OnWebsocketMessage(data)
  │
  ├─ 1. 解析JSON消息
  │     root = cJSON_Parse(data)
  │     message_type = root["type"]
  │
  ├─ 2. 根据类型路由
  │     ├─ "audio" → OnIncomingAudio()
  │     ├─ "text" → OnTextMessage()
  │     └─ "tool_result" → MCP处理
  │
  └─ 3. 触发对应回调
```

#### 10.3.3 发送音频流程

```cpp
SendAudio(packet)
  │
  ├─ 1. Base64编码
  │     encoded_data = Base64Encode(packet->data, packet->size)
  │
  ├─ 2. 封装为JSON
  │     json = {
  │       "type": "audio",
  │       "data": encoded_data,
  │       "format": "pcm16",
  │       "sample_rate": 16000
  │     }
  │
  └─ 3. WebSocket发送
        websocket_->Send(json_string)
```

### 10.4 协议选择

```
项目支持两种协议，由OTA配置决定:

MQTT协议:
  优点:
    ✓ 低带宽消耗 (UDP音频传输)
    ✓ 高效加密 (AES-CTR)
    ✓ 适合IoT设备
  缺点:
    ✗ 需要额外的UDP端口
    ✗ 配置较复杂

WebSocket协议:
  优点:
    ✓ 单一连接
    ✓ 配置简单
    ✓ 防火墙友好
  缺点:
    ✗ 带宽消耗较高 (Base64编码)
    ✗ CPU消耗较高

选择逻辑 (main/application.cc:550-564):
  if (ota_->HasMqttConfig())
      protocol_ = new MqttProtocol();
  else if (ota_->HasWebsocketConfig())
      protocol_ = new WebsocketProtocol();
  else
      protocol_ = new MqttProtocol();  // 默认MQTT
```

---

## 11. 完整交互流程示例

### 11.1 示例：用户触摸屏幕开始对话

```
┌─────────────────────────────────────────────────────────────┐
│  硬件触发 → 状态机转换 → 宠物同步 → 音频交互 → 完整流程    │
└─────────────────────────────────────────────────────────────┘

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第1阶段: 硬件触发
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. CST816S检测到触摸
   └─ INT引脚(GPIO11)产生中断

2. 167ms定时器读取触摸数据
   process_touch_swipe()
   ├─ esp_lcd_touch_read_data(touch_state.handle)
   ├─ 检测到按下: start_x=120, start_y=150
   └─ 记录 start_time

3. 用户释放触摸
   ├─ 计算 distance = 12px (< TAP_MAX_DISTANCE)
   ├─ 计算 duration = 320ms (< TAP_MAX_DURATION)
   └─ 判断: 轻触 (Tap)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第2阶段: 状态机转换
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

4. 触发聊天切换
   handle_tap()
   └─ Application::ToggleChatState()

5. 设备状态转换
   DeviceStateMachine::TransitionTo(kDeviceStateListening)
   ├─ 验证转换: Idle → Listening ✓
   ├─ 原子操作: current_state_.store(Listening)
   └─ ESP_LOGI: "State: idle -> listening"

6. 通知所有监听器
   NotifyStateChange(Idle, Listening)
   ├─ 回调1: xEventGroupSetBits(MAIN_EVENT_STATE_CHANGED)
   └─ 回调2: PetStateMachine::OnDeviceStateChanged()

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第3阶段: 宠物状态同步
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

7. 宠物状态机响应
   PetStateMachine::OnDeviceStateChanged(Idle, Listening)
   ├─ previous_pet_action_ = kIdle  // 保存当前动作
   ├─ in_voice_interaction_ = true
   └─ SetAction(kListening)

8. 动作映射到动画
   ActionToAnimation(kListening)
   └─ return "listen"

9. 切换动画
   SwitchAnimation("listen", loop=true)
   ├─ 停止当前动画定时器
   ├─ animation = {"listen", start=52, count=13, fps=15, loop=true}
   ├─ current_frame = 0
   ├─ 加载第一帧 (帧52)
   └─ 启动定时器 (167ms)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第4阶段: UI更新
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

10. 事件处理
    MAIN_EVENT_STATE_CHANGED触发
    └─ HandleStateChangedEvent()
        ├─ LED指示灯: 变为绿色闪烁
        ├─ 底部状态栏: "正在聆听..."
        └─ 屏幕: 播放 listen 动画

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第5阶段: 音频通道建立
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

11. 打开音频通道
    Protocol::OpenAudioChannel()
    ├─ MQTT协议:
    │   ├─ 建立UDP连接到音频服务器
    │   └─ 初始化AES加密上下文
    └─ WebSocket协议:
        └─ 切换到音频模式

12. 发送开始聆听消息
    Protocol::SendStartListening(kListeningModeManualStop)
    └─ 发送JSON: {
          "type": "start_listening",
          "mode": "manual_stop"
        }

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第6阶段: 录音与发送
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

13. 开始录音
    AudioService::StartRecording()
    ├─ I2S麦克风开始采集
    ├─ VAD检测语音活动
    └─ 音频数据入队 send_queue_

14. 发送音频数据
    MAIN_EVENT_SEND_AUDIO触发
    while (packet = PopPacketFromSendQueue()) {
        protocol_->SendAudio(packet)
        ├─ MQTT: AES加密 + UDP发送
        └─ WebSocket: Base64编码 + WS发送
    }

15. 用户说话
    "今天天气怎么样？"
    └─ 持续发送音频数据到云端 (~3秒)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第7阶段: AI处理与回复
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

16. 云端AI处理
    ├─ 语音识别: "今天天气怎么样？"
    ├─ 意图理解: 查询天气
    ├─ 可能调用MCP工具查询
    └─ 生成回复: "今天北京晴天，温度22度，适合出门哦~"

17. 设备接收文本消息
    Protocol::OnTextMessage(json)
    ├─ 如果是工具调用 → McpServer处理
    └─ 如果是普通文本 → 直接显示

18. 状态转换到Speaking
    DeviceStateMachine::TransitionTo(kDeviceStateSpeaking)
    └─ NotifyStateChange(Listening, Speaking)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第8阶段: 宠物状态更新
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

19. 宠物状态机响应
    PetStateMachine::OnDeviceStateChanged(Listening, Speaking)
    └─ SetAction(kSpeaking)
        └─ ActionToAnimation(kSpeaking) → "talk"

20. 切换动画
    SwitchAnimation("talk", loop=true)
    └─ 播放 talk 动画 (帧13-25)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第9阶段: 播放AI语音
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

21. 接收AI语音数据
    Protocol::OnIncomingAudio(packet)
    └─ audio_service_.PushPacketToDecodeQueue(packet)

22. 播放语音
    AudioService::PlayAudio()
    ├─ 解码音频数据 (Opus/PCM)
    ├─ I2S扬声器播放
    └─ 用户听到: "今天北京晴天，温度22度，适合出门哦~"

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
第10阶段: 对话结束
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

23. 语音播放完成
    └─ OnAudioPlaybackComplete()

24. 状态转换回Idle
    DeviceStateMachine::TransitionTo(kDeviceStateIdle)
    └─ NotifyStateChange(Speaking, Idle)

25. 宠物状态恢复
    PetStateMachine::OnDeviceStateChanged(Speaking, Idle)
    ├─ in_voice_interaction_ = false
    └─ SetAction(previous_pet_action_)  // 恢复kIdle

26. 对话结束奖励
    PetStateMachine::OnConversationEnd()
    ├─ rand = esp_random() % 100 = 45
    ├─ 45 < 60 → 自动玩耍
    ├─ happiness = Clamp(80 + 8) = 88
    └─ ESP_LOGI: "Conversation → auto play (+8 happiness)"

27. 金币奖励
    CoinSystem::OnChatMessage()
    ├─ daily_chat_count_++ = 9
    └─ 9 % 16 != 0 → 不奖励金币 (需16句)

28. 动画恢复
    SwitchAnimation("idle", loop=true)
    └─ 播放 idle 动画 (帧0-12)

29. 关闭音频通道
    Protocol::CloseAudioChannel()
    ├─ 释放UDP连接 (MQTT)
    └─ 切换回普通模式 (WebSocket)

30. 用户看到结果
    ├─ 屏幕: 播放idle动画
    ├─ 底部栏: 显示宠物状态 (happiness=88)
    └─ LED: 恢复默认颜色
```

### 11.2 示例：AI通过MCP切换背景

```
┌─────────────────────────────────────────────────────────────┐
│  用户语音 → AI理解 → MCP调用 → 背景切换 → 完整流程          │
└─────────────────────────────────────────────────────────────┘

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段1: 用户发起请求
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

1. 用户触摸屏幕开始对话
   (参考11.1示例的步骤1-15)

2. 用户说话
   "切换到夜晚背景"
   └─ 音频持续发送到云端

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段2: AI理解与工具调用
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

3. 云端AI处理
   ├─ 语音识别: "切换到夜晚背景"
   ├─ 意图理解:
   │   ├─ 关键词: "切换" + "夜晚" + "背景"
   │   ├─ 目标: 更改背景
   │   └─ 工具: background
   └─ 生成工具调用请求

4. AI生成JSON-RPC请求
   {
     "jsonrpc": "2.0",
     "method": "tools/call",
     "params": {
       "name": "background",
       "arguments": {
         "action": "set",
         "name": "night"
       }
     },
     "id": 12345
   }

5. 发送到设备
   Protocol::SendText(json_string)
   └─ MQTT: 发布到 "/push/{device_id}"
      WebSocket: WS发送

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段3: 设备接收并解析
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

6. 设备接收消息
   Protocol::OnTextMessage(json_string)
   ├─ ESP_LOGI: "Received message: {\"jsonrpc\":\"2.0\",...}"
   └─ 触发回调 → 传递给MCP Server

7. MCP Server解析
   McpServer::ParseMessage(json_string)
   ├─ root = cJSON_Parse(json_string)
   ├─ method = root["method"] = "tools/call"
   ├─ tool_name = root["params"]["name"] = "background"
   ├─ arguments = root["params"]["arguments"]
   │   ├─ action = "set"
   │   └─ name = "night"
   └─ id = root["id"] = 12345

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段4: 执行工具调用
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

8. 路由到工具
   McpServer::DoToolCall(12345, "background", arguments)
   ├─ 查找工具: tools_.find("background") ✓
   ├─ 解析参数:
   │   ├─ PropertyList props
   │   ├─ props["action"].set_value("set")
   │   └─ props["name"].set_value("night")
   └─ 调用回调函数

9. Background工具回调
   HandleSetBackground("night")
   │
   ├─ 查找背景映射:
   │   for (i = 0; i < kBackgroundMapSize; i++)
   │     if (kBackgroundMap[i].name == "night")
   │       → found_idx = 3 (BG_TIME_NIGHT)
   │       → description = "night"
   │       → category = "time"
   │
   ├─ 检查解锁:
   │   if (category == "style")
   │     → 检查 achievements.IsXxxUnlocked()
   │   else if (category == "time")
   │     → 始终可用 ✓
   │
   └─ 强制设置:
       BackgroundManager::ForceBackground(3)
       ├─ force_enabled_ = true
       ├─ forced_background_ = 3
       └─ ESP_LOGI: "Force background: 3"

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段5: 背景加载与显示
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

10. 背景立即更新
    GetCurrentBackground()
    ├─ 优先级1: if (force_enabled_) ✓
    ├─ return forced_background_ (3)
    └─ ESP_LOGI: "Force background: 3"

11. 加载夜晚背景
    BackgroundLoader::LoadBackground(3)
    │
    ├─ 计算Flash偏移:
    │   animation_data = 104 × 26368 = 2,742,272
    │   background_offset = animation_data + (3 × 67968)
    │                     = 2,742,272 + 203,904
    │                     = 2,946,176
    │
    ├─ 读取背景数据:
    │   esp_partition_read(partition, 2946176, buffer, 67968)
    │   └─ 读取 280×240 像素数据 (含调色板)
    │
    ├─ 解码调色板:
    │   for (i = 0; i < 256; i++)
    │     palette[i] = RGB888_TO_RGB565(palette_rgb888[i])
    │
    └─ 解码像素:
        for (i = 0; i < 280*240; i++)
          rgb565_buffer[i] = palette[pixel_indices[i]]

12. 更新显示
    ├─ lv_img_set_src(bg_img, rgb565_buffer)
    │   └─ LVGL更新背景图片对象
    │
    └─ lv_refr_now(NULL)
        └─ 立即刷新屏幕
            → 用户看到夜晚背景（星空）

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段6: 返回结果
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

13. 工具返回结果
    return std::string("Switched to background: night");

14. MCP封装结果
    McpServer::ReplyResult(12345, "Switched to background: night")
    {
      "jsonrpc": "2.0",
      "result": {
        "content": [{
          "type": "text",
          "text": "Switched to background: night"
        }],
        "isError": false
      },
      "id": 12345
    }

15. 发送回AI
    Protocol::SendText(result_json)
    └─ MQTT/WebSocket 发送

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
阶段7: AI回复用户
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

16. AI接收工具结果
    └─ 解析: "Switched to background: night"

17. AI生成自然语言回复
    "好的，已经切换到夜晚背景了，星空很美吧~"

18. 语音合成并播放
    (参考11.1示例的步骤21-30)

19. 用户体验
    ├─ 听到AI语音回复
    ├─ 看到夜晚星空背景
    └─ 底部栏显示宠物状态
```

---

## 12. 关键技术要点总结

### 12.1 状态机设计原则

```
✓ 严格验证状态转换 (IsValidTransition)
✓ 观察者模式通知机制 (AddStateChangeListener)
✓ 原子操作保证线程安全 (std::atomic)
✓ 详细的日志记录 (ESP_LOGI)
✓ 无锁化设计 (尽量避免mutex)
```

### 12.2 事件驱动架构优点

```
✓ FreeRTOS事件组 (xEventGroupWaitBits)
✓ 事件触发而非轮询（节省CPU）
✓ 多事件并发处理
✓ 优先级通过代码顺序控制
✓ 易于扩展新事件
```

### 12.3 背景选择优先级（修正后）

```
1. Force Mode (MCP强制指定)
   └─ 最高优先级，用户/AI明确指定

2. Festival (节日全天显示)
   └─ 特殊日期，营造节日氛围

3. Weather (天气独立背景)
   └─ MCP设置，独立替换所有背景

4. Time/Style (时间段随机)
   └─ 只在时间段切换时触发20%随机
       持续到下次时间段切换
```

### 12.4 触摸检测优化策略

```
✓ 定时器检测 (167ms) - 避免中断风暴
✓ 参数可调 (TAP_MAX_DISTANCE等) - 适应不同场景
✓ 区分轻触/滑动 - 不同交互方式
✓ 智能日志级别 - 初始化时INFO，运行时ERROR
✓ 休眠正常 (NACK错误) - 节省电力
```

### 12.5 MCP工具设计规范

```
✓ JSON-RPC 2.0标准协议
✓ 工具注册机制 (AddTool)
✓ 参数类型验证 (PropertyType)
✓ 范围检查 (min/max)
✓ 错误处理和返回格式化
✓ 用户专用工具 (AddUserOnlyTool)
```

### 12.6 动画系统优化技术

```
✓ 256色调色板 - 减少75%存储空间
✓ Flash分区读取 - 避免ESP32-C6 mmap限制
✓ 透明背景支持 - Alpha通道合成
✓ 定时器同步 - 稳定的帧率 (6fps)
✓ 循环播放 - 节省存储空间
✓ 动画预加载 - 减少切换延迟
```

### 12.7 通信协议优化

```
MQTT:
  ✓ AES-CTR加密 - 保证安全性
  ✓ UDP音频传输 - 低延迟
  ✓ 序列号防重放 - 安全性
  ✓ 低带宽消耗 - 适合IoT

WebSocket:
  ✓ 单一连接 - 简化配置
  ✓ 防火墙友好 - 兼容性好
  ✓ 实时双向通信 - 低延迟
```

---

## 13. 常见问题调试指南

### 13.1 触摸无反应

**症状**：
- 触摸屏幕没有反应
- 无法切换对话状态
- 滑动无法触发摸头动画

**检查点**：
```bash
# 1. 查看日志是否有触摸初始化成功
I (xxxx) waveshare_lcd_1_69: Touch panel initialized successfully (handle=0x3fc9xxxx)

# 2. 查看动画定时器是否启动
I (xxxx) waveshare_lcd_1_69: Animation timer started (167 ms interval)

# 3. 查看是否有触摸错误
W (xxxx) waveshare_lcd_1_69: Touch not initialized (init=1, handle=(nil))
```

**解决方法**：
1. 检查I2C连接 (GPIO 7/8)
2. 检查INT引脚 (GPIO 11)
3. 检查触摸芯片供电
4. 重新上电设备
5. 检查日志级别设置

### 13.2 背景不按预期切换

**症状**：
- 时间段切换但背景不变
- MCP设置背景无效
- 背景频繁变化

**检查点**：
```bash
# 1. 确认时间段是否真的改变
I (xxxx) BackgroundManager: Time period changed: 1 -> 2 (hour=17)

# 2. 查看背景选择日志
I (xxxx) BackgroundManager: Time background selected: 1 (hour=17)
# 或
I (xxxx) BackgroundManager: Style background selected (20% random): 12

# 3. 检查Force模式
I (xxxx) BackgroundManager: Force background: 3
```

**调试方法**：
```cpp
// 手动触发时间切换
auto& bg_mgr = BackgroundManager::GetInstance();
bg_mgr.UpdateTime(17, 0, 1, 25);  // 切换到sunset时间段
uint16_t bg = bg_mgr.GetCurrentBackground();
ESP_LOGI(TAG, "Current background: %d", bg);
```

**常见原因**：
1. 时间段未真正切换（在同一时间段内）
2. 背景未解锁（风格/节日背景）
3. Force模式被激活
4. 天气模式覆盖

### 13.3 MCP工具调用失败

**症状**：
- AI调用工具无响应
- 工具返回错误
- 参数解析失败

**检查点**：
```bash
# 1. 查看MCP收到的消息
ESP_LOGI(TAG, "MCP received: %s", json_string);

# 2. 查看工具调用
ESP_LOGI(TAG, "Tool: %s", tool_name);
ESP_LOGI(TAG, "Args: %s", args_string);

# 3. 查看工具执行结果
ESP_LOGI(TAG, "Tool result: %s", result);
```

**调试方法**：
```cpp
// 添加详细日志
McpServer::DoToolCall(...) {
    ESP_LOGI(TAG, "=== MCP Tool Call ===");
    ESP_LOGI(TAG, "ID: %d", id);
    ESP_LOGI(TAG, "Tool: %s", tool_name.c_str());
    ESP_LOGI(TAG, "Arguments: %s", cJSON_Print(arguments));

    // 执行工具...

    ESP_LOGI(TAG, "Result: %s", result.c_str());
    ESP_LOGI(TAG, "=== End Tool Call ===");
}
```

**常见原因**：
1. JSON格式错误
2. 工具名称拼写错误
3. 参数缺失或类型错误
4. 工具未注册

### 13.4 宠物状态异常

**症状**：
- 宠物动作不切换
- 属性不衰减
- 对话奖励无效

**检查点**：
```bash
# 1. 查看宠物初始化
I (xxxx) PetState: Pet initialized: hunger=80, happiness=80, cleanliness=80, health=100

# 2. 查看Tick调用
I (xxxx) PetState: Tick: hunger=79, happiness=79, cleanliness=79, health=100

# 3. 查看动作切换
I (xxxx) PetState: SetAction: listening -> animation: listen, duration: 0 ms
```

**调试方法**：
```cpp
// 手动触发Tick
auto& pet = PetStateMachine::GetInstance();
pet.Tick();
const auto& stats = pet.GetStats();
ESP_LOGI(TAG, "Stats: h=%d, ha=%d, c=%d, he=%d",
         stats.hunger, stats.happiness,
         stats.cleanliness, stats.health);
```

### 13.5 动画不播放或卡顿

**症状**：
- 动画不显示
- 帧率低或卡顿
- 动画不循环

**检查点**：
```bash
# 1. 查看动画定时器
I (xxxx) waveshare_lcd_1_69: Animation timer started (167 ms interval)

# 2. 查看帧加载
I (xxxx) AnimLoader: Reading frame 13 (talk animation)

# 3. 查看动画切换
I (xxxx) waveshare_lcd_1_69: Switching to animation: talk (loop=true)
```

**调试方法**：
```cpp
// 检查帧加载性能
uint32_t start = esp_timer_get_time();
AnimationLoader::ReadAndDecodeFrame(frame_idx);
uint32_t end = esp_timer_get_time();
ESP_LOGI(TAG, "Frame decode time: %lu us", end - start);
```

**常见原因**：
1. Flash读取慢
2. 定时器未启动
3. 动画资源未烧录
4. 内存不足

---

## 14. 性能优化建议

### 14.1 内存优化

```
当前内存使用:
  - AnimationLoader缓冲区: ~200KB
  - BackgroundLoader缓冲区: ~200KB
  - LVGL显示缓冲区: ~300KB
  - 总计: ~700KB

优化建议:
  ✓ 释放不用的透明缓冲区 (FreeTransparentBuffers)
  ✓ 使用单缓冲而非双缓冲显示
  ✓ 按需加载动画帧
  ✓ 定期检查堆碎片 (SystemInfo::PrintHeapStats)
```

### 14.2 CPU优化

```
当前CPU消耗:
  - 动画解码: ~10% (167ms定时器)
  - 音频处理: ~20% (录音+播放)
  - 网络通信: ~5%
  - UI更新: ~5%

优化建议:
  ✓ 降低动画帧率 (6fps → 4fps)
  ✓ 使用硬件AES加速
  ✓ 优化调色板转换算法
  ✓ 减少日志输出
```

### 14.3 网络优化

```
当前带宽消耗:
  - 音频上行: ~16KB/s (16kHz PCM)
  - 音频下行: ~8KB/s (压缩后)
  - MCP消息: ~1KB/s

优化建议:
  ✓ 使用Opus编码压缩音频
  ✓ 启用AES-CTR加密 (MQTT)
  ✓ 批量发送MCP消息
  ✓ 压缩JSON消息
```

---

## 15. 版本历史

### v1.0 (2026-01-25)
- ✅ 创建系统架构文档
- ✅ 详细说明状态机系统
- ✅ 修正背景选择逻辑文档
- ✅ 添加触摸交互机制说明
- ✅ 添加MCP AI调用流程
- ✅ 添加动画系统详解
- ✅ 添加通信协议说明
- ✅ 添加完整交互示例
- ✅ 添加调试指南

---

## 附录A: 文件索引

### 核心文件快速查找

| 功能模块 | 文件路径 | 关键函数/类 |
|---------|---------|-----------|
| 主应用 | `main/application.cc` | `Application::Run()` |
| 设备状态机 | `main/device_state_machine.cc` | `DeviceStateMachine::TransitionTo()` |
| 宠物状态机 | `main/pet/pet_state.cc` | `PetStateMachine::Tick()` |
| 背景管理 | `main/images/background_manager.cc` | `BackgroundManager::GetCurrentBackground()` |
| 动画加载 | `main/images/animation_loader.cc` | `AnimationLoader::ReadAndDecodeFrame()` |
| MCP服务器 | `main/mcp_server.cc` | `McpServer::ParseMessage()` |
| MQTT协议 | `main/protocols/mqtt_protocol.cc` | `MqttProtocol::SendAudio()` |
| WebSocket协议 | `main/protocols/websocket_protocol.cc` | `WebsocketProtocol::SendAudio()` |
| 触摸驱动 | `main/boards/waveshare-c6-lcd-1.69/esp32-c6-lcd-1.69.cc` | `process_touch_swipe()` |

---

## 附录B: 参考文档

- [改动说明.md](./改动说明.md) - 完整的改动记录
- [触摸和状态栏修复说明.md](./触摸和状态栏修复说明.md) - 触摸修复详情
- [GIF_ANIMATION_GUIDE.md](./GIF_ANIMATION_GUIDE.md) - GIF动画实现指南
- [PET_SYSTEM_GUIDE.md](./PET_SYSTEM_GUIDE.md) - 宠物系统详细指南
- [MEMORY_IMPLEMENTATION_GUIDE.md](./MEMORY_IMPLEMENTATION_GUIDE.md) - 记忆系统指南

---

**文档结束**

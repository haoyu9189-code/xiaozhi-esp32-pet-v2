# 日程添加死锁 Bug 修复

**发现日期**：2026-02-02
**Bug 等级**：🔴 **严重（Critical）**
**影响**：导致日程添加功能完全不可用，系统挂起

---

## Bug 现象

### 用户报告
用户说："帮我记一下，明天上午10点去学校办理实验"
年糕回复："又没记住…系统好像卡住了"

### 日志表现
```
I (48305) MemMcpTools: Memory tool called: action=write ✅
I (48305) MemMcpTools: Schedule write request: content='前往学校收拾时间', datetime='2026-02-03 10:00', repeat='' ✅
I (48305) MemMcpTools: Checking schedule conflict for 2026-02-03 10:00... ✅
I (48315) MemMcpTools: Conflict check took ld us (0.0 ms) ⚠️
I (48315) MemMcpTools: Adding schedule to storage... ✅
[无后续日志] ❌ <- 系统在此处挂起
```

---

## 根本原因

### 死锁分析

**问题代码**（`main/memory/memory_storage.cc`）：

```cpp
bool MemoryStorage::AddEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);  // ← 第1次加锁
    ...
    events_cache_.push_back(event);
    events_dirty_ = true;

    SaveEvents();  // ← 调用 SaveEvents()
    ...
    return true;
}

void MemoryStorage::SaveEvents() {
    if (!events_dirty_) return;

    std::lock_guard<std::mutex> lock(mutex_);  // ← 第2次加锁 - 死锁！
    ...
}
```

### 死锁原理

1. `AddEvent()` 在第1180行获取 `mutex_` 锁
2. `AddEvent()` 在第1191行调用 `SaveEvents()`
3. `SaveEvents()` 在第1227行尝试再次获取 `mutex_` 锁
4. **死锁发生**：线程永久等待，无法释放或获取锁

### 为什么之前没发现

- Phase 1-3（日程冲突检测）的代码都没有触发这个死锁
- 直到用户真正尝试添加日程时才暴露

---

## 修复方案

### 方案选择

**选择**：移除 `SaveEvents()` 中的锁（✅ 已实施）

**理由**：
- 所有调用 `SaveEvents()` 的地方都已经持有 `mutex_` 锁
- 这是最简单和最安全的解决方案
- 不影响其他代码

### 修复代码

**文件**：`main/memory/memory_storage.cc`

**修改前**：
```cpp
void MemoryStorage::SaveEvents() {
    if (!events_dirty_) return;

    std::lock_guard<std::mutex> lock(mutex_);

    nvs_handle_t handle;
    ...
}
```

**修改后**：
```cpp
void MemoryStorage::SaveEvents() {
    if (!events_dirty_) return;

    // NOTE: Caller must hold mutex_! Do not add lock here to avoid deadlock.
    // This method is always called from within locked context.

    nvs_handle_t handle;
    ...
}
```

---

## 其他修复

### 1. 日志格式化错误

**问题**：`%lld` 在 ESP32 上不支持，导致显示 `ld us` 而不是实际数字

**修复**：
```cpp
// 修改前
ESP_LOGI("MemMcpTools", "Conflict check took %lld us (%.1f ms)", elapsed_us, elapsed_us / 1000.0);

// 修改后
ESP_LOGI("MemMcpTools", "Conflict check took %d us", (int)elapsed_us);
```

### 2. 增强调试日志

**添加的日志点**：
- MCP 工具入口
- Schedule 处理详细步骤
- 冲突检测性能监控
- AddEvent 调用前后状态

---

## 验证测试

### 预期修复后的日志

```
I (xxx) MemMcpTools: Memory tool called: action=write
I (xxx) MemMcpTools: Schedule write request: content='去学校办理实验', datetime='2026-02-03 10:00', repeat=''
I (xxx) MemMcpTools: Checking schedule conflict for 2026-02-03 10:00...
I (xxx) MemMcpTools: Conflict check took 1234 us
I (xxx) MemMcpTools: Adding schedule to storage...
I (xxx) MemMcpTools: Calling storage.AddEvent() - before
I (xxx) Memory: Added schedule: 去学校办理实验 at 2026-02-03 10:00
I (xxx) MemMcpTools: Calling storage.AddEvent() - after, result=1
I (xxx) MemMcpTools: Schedule added successfully: added: schedule '去学校办理实验' at 2026-02-03 10:00
```

### 测试步骤

1. 编译并烧录固件：`idf.py build flash monitor`
2. 对年糕说："帮我记一下，明天上午10点去学校办理实验"
3. 确认日志中出现 "Schedule added successfully"
4. 查询日程：`memory(action='read', type='schedule')`
5. 确认日程已保存

---

## 影响范围

### 受影响的功能
- ✅ 日程添加功能（**完全不可用** → **已修复**）
- ✅ 重复日程功能（依赖 AddEvent）
- ✅ 日程冲突检测（功能正常，但添加失败）

### 未受影响的功能
- ✅ 日程查询（`memory(action='read', type='schedule')`）
- ✅ 日程删除（使用不同的锁机制）
- ✅ 其他记忆功能（Facts, Moments, etc.）

---

## 预防措施

### 代码审查要点

1. **避免嵌套锁**：不要在持有锁的情况下调用会再次加锁的方法
2. **文档化锁要求**：在方法注释中明确说明是否需要持有锁
3. **使用 RAII**：确保锁总是被正确释放
4. **测试覆盖**：添加集成测试，覆盖完整的调用链

### 建议的代码规范

```cpp
// ✅ 好的做法：公共方法加锁，调用不加锁的私有方法
void MemoryStorage::AddEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    AddEventUnlocked(event);  // 调用不加锁的内部方法
}

// ❌ 坏的做法：公共方法加锁，调用也加锁的方法
void MemoryStorage::AddEvent(const Event& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    SaveEvents();  // SaveEvents 内部也加锁 - 死锁！
}
```

---

## 相关问题排查

### 是否有其他 Save*() 方法有类似问题？

**检查结果**：❌ **无其他死锁**

其他 `Save*()` 方法（SaveProfile, SaveFamily, SaveFacts, etc.）都没有在内部加锁，因此不会导致死锁。

---

## 总结

### Bug 严重性
- 🔴 **Critical**：导致核心功能完全不可用
- 💥 **影响用户体验**：用户无法添加日程，系统看起来"卡死"
- 🎯 **容易触发**：任何日程添加操作都会触发

### 修复难度
- ✅ **修复简单**：只需移除一行代码中的锁
- ⏱️ **修复耗时**：10分钟（诊断：2小时）
- 🧪 **测试验证**：简单，可立即验证

### 经验教训
1. **锁的设计要小心**：嵌套锁是常见的死锁原因
2. **测试要全面**：集成测试应覆盖完整的用户场景
3. **日志要详细**：详细的性能日志帮助快速定位问题
4. **代码审查重要**：锁相关的代码需要特别仔细审查

---

**修复状态**：✅ **已修复**
**验证状态**：⏳ **待测试**
**文档更新**：✅ **已完成**

**下一步**：重新编译并测试，确认问题已解决。

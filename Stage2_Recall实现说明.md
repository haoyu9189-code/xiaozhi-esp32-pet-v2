# Stage 2: 深层记忆检索（Recall）实现说明

**实施日期**：2026-02-02
**功能**：从归档中检索历史记忆，让年糕能"回忆"起更早的对话

---

## 一、实现概述

Stage 2 在 Stage 1 归档功能的基础上，添加了智能检索能力，支持三种检索方式：
1. **按关键词搜索**：不区分大小写的子串匹配
2. **按时间范围查询**：指定起止日期检索特定时间段的记忆
3. **检索最近N条**：获取最近归档的记忆（默认10条）

---

## 二、核心功能

### 2.1 检索方法

#### RecallByKeyword（关键词搜索）

```cpp
std::vector<ArchivedItem> RecallByKeyword(
    const char* type,      // fact/moment/event
    const char* keyword,   // 搜索关键词
    int limit = 10         // 最多返回条数
);
```

**使用场景**：
- "你还记得我去北京的事吗" → `keyword='北京'`
- "我们聊过音乐吗" → `keyword='音乐'`

**实现原理**：
- 逐行读取 JSONL 文件
- 不区分大小写的子串匹配
- 匹配到 limit 条后停止扫描

#### RecallByTimeRange（时间范围查询）

```cpp
std::vector<ArchivedItem> RecallByTimeRange(
    const char* type,           // fact/moment/event
    const char* start_date,     // YYYY-MM-DD（可为空）
    const char* end_date,       // YYYY-MM-DD（可为空）
    int limit = 10              // 最多返回条数
);
```

**使用场景**：
- "去年的事你还记得吗" → `start_date='2025-01-01', end_date='2025-12-31'`
- "上个月我们聊过什么" → `start_date='2026-01-01', end_date='2026-01-31'`

**实现原理**：
- 利用 ISO 8601 时间戳格式的字符串排序特性
- 简单的字符串前10位比较（YYYY-MM-DD）
- start_date/end_date 可为空（表示不限制）

#### RecallRecent（最近N条）

```cpp
std::vector<ArchivedItem> RecallRecent(
    const char* type,      // fact/moment/event
    int limit = 10         // 最多返回条数
);
```

**使用场景**：
- "记得我们以前聊过什么吗" → 默认检索最近10条
- "最近归档的记忆有哪些" → 查看最新归档

**实现原理**：
- 读取所有归档行到内存（JSONL是追加格式，最后的最新）
- 取最后 N 条返回

---

## 三、MCP 工具集成

### 3.1 新增 recall 操作

**action**: `recall`

**参数**：
- `type`（必须）：fact / moment / event
- `keyword`（可选）：搜索关键词
- `start_date`（可选）：起始日期 YYYY-MM-DD
- `end_date`（可选）：结束日期 YYYY-MM-DD
- `limit`（可选）：返回条数，默认 10

### 3.2 使用示例

#### 示例1：按关键词搜索

**请求**：
```python
memory(action='recall', type='fact', keyword='北京')
```

**返回**：
```json
{
  "type": "fact",
  "count": 2,
  "items": [
    {
      "timestamp": "2026-01-15T10:30:00",
      "type": "fact",
      "content": "主人喜欢吃北京烤鸭"
    },
    {
      "timestamp": "2026-01-17T09:00:00",
      "type": "fact",
      "content": "主人在北京工作"
    }
  ]
}
```

#### 示例2：按时间范围查询

**请求**：
```python
memory(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31')
```

**返回**：
```json
{
  "type": "moment",
  "count": 3,
  "items": [
    {
      "timestamp": "2025-03-15T18:00:00",
      "type": "moment",
      "topic": "生日庆祝",
      "content": "主人25岁生日聚会",
      "emotion_type": 1,
      "emotion_intensity": 5,
      "importance": 5
    }
  ]
}
```

#### 示例3：检索最近归档

**请求**：
```python
memory(action='recall', type='fact', limit=20)
```

**返回**：最近归档的20条Facts

---

## 四、修改的文件

### 4.1 [main/memory/memory_archive.h](main/memory/memory_archive.h)

**修改内容**：
- 添加 3 个公共检索方法：RecallByTimeRange, RecallByKeyword, RecallRecent
- 添加 4 个私有辅助方法：GetArchiveFilename, ParseArchivedItem, MatchesTimeRange, ContainsKeyword

**关键代码**：
```cpp
// Recall operations (Stage 2)
std::vector<ArchivedItem> RecallByTimeRange(const char* type,
    const char* start_date, const char* end_date, int limit = 10);
std::vector<ArchivedItem> RecallByKeyword(const char* type,
    const char* keyword, int limit = 10);
std::vector<ArchivedItem> RecallRecent(const char* type, int limit = 10);
```

### 4.2 [main/memory/memory_archive.cc](main/memory/memory_archive.cc)

**修改内容**：
- 实现 3 个检索方法（共约 200 行代码）
- 实现 4 个辅助方法
- 添加详细的日志输出（扫描行数、匹配数量、耗时等）

**关键实现**：

#### GetArchiveFilename（统一文件名管理）

```cpp
const char* MemoryArchive::GetArchiveFilename(const char* type) {
    if (strcmp(type, "fact") == 0) return FACTS_ARCHIVE;
    else if (strcmp(type, "moment") == 0) return MOMENTS_ARCHIVE;
    else if (strcmp(type, "event") == 0) return EVENTS_ARCHIVE;
    return nullptr;
}
```

#### ParseArchivedItem（JSONL 解析）

```cpp
bool MemoryArchive::ParseArchivedItem(const char* json_line, ArchivedItem& item) {
    cJSON* root = cJSON_Parse(json_line);
    if (!root) return false;

    // Extract timestamp
    cJSON* timestamp = cJSON_GetObjectItem(root, "timestamp");
    if (timestamp && cJSON_IsString(timestamp)) {
        strncpy(item.timestamp, timestamp->valuestring, sizeof(item.timestamp) - 1);
    }

    // Extract type
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type && cJSON_IsString(type)) {
        strncpy(item.type, type->valuestring, sizeof(item.type) - 1);
    }

    // Store entire JSON as content
    strncpy(item.content, json_line, sizeof(item.content) - 1);

    cJSON_Delete(root);
    return true;
}
```

#### MatchesTimeRange（时间过滤）

```cpp
bool MemoryArchive::MatchesTimeRange(const char* timestamp,
                                     const char* start_date,
                                     const char* end_date) {
    // timestamp format: YYYY-MM-DDTHH:MM:SS
    // date format: YYYY-MM-DD
    // Simple string comparison works due to ISO 8601 format

    if (start_date && strlen(start_date) > 0) {
        if (strncmp(timestamp, start_date, 10) < 0) {
            return false;
        }
    }

    if (end_date && strlen(end_date) > 0) {
        if (strncmp(timestamp, end_date, 10) > 0) {
            return false;
        }
    }

    return true;
}
```

#### ContainsKeyword（关键词匹配）

```cpp
bool MemoryArchive::ContainsKeyword(const char* content, const char* keyword) {
    if (!keyword || strlen(keyword) == 0) {
        return true;  // Empty keyword matches everything
    }

    // Case-insensitive search
    std::string content_lower(content);
    std::string keyword_lower(keyword);

    // Convert to lowercase
    for (char& c : content_lower) {
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
    }
    for (char& c : keyword_lower) {
        if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
    }

    return content_lower.find(keyword_lower) != std::string::npos;
}
```

### 4.3 [main/memory/memory_mcp_tools.cc](main/memory/memory_mcp_tools.cc)

**修改内容**：
1. 添加 `#include "memory_archive.h"`
2. 实现 HandleRecall 函数（约 80 行）
3. 更新 MCP 工具描述（添加 recall 操作说明）
4. 添加 4 个新参数：keyword, start_date, end_date, limit
5. 添加 recall 操作处理分支

**关键代码**：

#### HandleRecall 函数

```cpp
static cJSON* HandleRecall(const std::string& type, const std::string& start_date,
                           const std::string& end_date, const std::string& keyword, int limit) {
    auto& archive = MemoryArchive::GetInstance();

    if (!archive.IsInitialized()) {
        cJSON* error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "Archive not initialized");
        return error;
    }

    std::vector<ArchivedItem> results;

    // Determine recall method based on parameters
    if (!keyword.empty()) {
        // Keyword search
        results = archive.RecallByKeyword(type.c_str(), keyword.c_str(), limit);
    }
    else if (!start_date.empty() || !end_date.empty()) {
        // Time range search
        results = archive.RecallByTimeRange(type.c_str(),
                                           start_date.empty() ? nullptr : start_date.c_str(),
                                           end_date.empty() ? nullptr : end_date.c_str(),
                                           limit);
    }
    else {
        // Recent items (default)
        results = archive.RecallRecent(type.c_str(), limit);
    }

    // Build JSON response
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type.c_str());
    cJSON_AddNumberToObject(root, "count", (int)results.size());

    cJSON* items = cJSON_CreateArray();
    for (const auto& item : results) {
        cJSON* content_json = cJSON_Parse(item.content);
        if (content_json) {
            cJSON_AddItemToArray(items, content_json);
        }
    }
    cJSON_AddItemToObject(root, "items", items);

    return root;
}
```

#### MCP 工具描述更新

**添加到工具描述**：
```text
- recall: Retrieve archived memories from long-term storage
  Required: type (fact/moment/event)
  Optional: keyword (search text), start_date (YYYY-MM-DD), end_date (YYYY-MM-DD), limit (default 10)
  Methods: (1) By keyword: recall(action='recall', type='fact', keyword='北京')
           (2) By time range: recall(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31')
           (3) Recent items: recall(action='recall', type='fact', limit=20)
```

**添加示例**：
```python
memory(action='recall', type='fact', keyword='北京')
memory(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31')
memory(action='recall', type='fact', limit=20)
```

### 4.4 [system_prompt.txt](system_prompt.txt)

**修改内容**：在【工具使用】部分添加 recall 说明

**添加内容**：
```text
- 主人问旧事/想回忆：调用memory的recall查询归档记忆
  * 按关键词：memory(action='recall', type='fact', keyword='北京') - 搜索包含"北京"的旧事实
  * 按时间：memory(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31') - 查询2025年的特殊时刻
  * 最近归档：memory(action='recall', type='fact', limit=20) - 获取最近归档的20条事实
  * 支持类型：fact（事实）、moment（特殊时刻）、event（重要事件）
  * 用法：主人说"记得我们以前聊过什么吗"、"你还记得我去北京的事吗"、"去年的事你还记得吗"等，就用recall查询归档
```

### 4.5 [记忆系统架构说明.md](记忆系统架构说明.md)

**修改内容**：
1. 更新核心特性：添加智能检索
2. 更新架构图：Memory Archive 显示"归档+检索"
3. 添加完整的"十二、深层记忆归档系统"章节（约 300 行）

**新增章节内容**：
- 12.1 概述
- 12.2 文件结构
- 12.3 数据结构
- 12.4 归档文件格式
- 12.5 核心方法（Stage 1 + Stage 2）
- 12.6 MCP 工具集成
- 12.7 自动归档流程
- 12.8 检索性能优化
- 12.9 存储容量规划
- 12.10 System Prompt 使用说明
- 12.11 实现细节
- 12.12 测试验证
- 12.13 已知限制
- 12.14 未来扩展

---

## 五、性能优化

### 5.1 流式读取

- 逐行读取 JSONL 文件，避免一次性加载全部内容
- 内存占用：单行 512 字节，临时缓冲
- 适合大文件（数千条记忆）

### 5.2 提前终止

- 达到 limit 数量后立即停止扫描
- 平均扫描行数：N/2（假设均匀分布）
- 减少不必要的 I/O 和解析

### 5.3 时间戳优化

- JSONL 追加格式，时间戳天然有序
- ISO 8601 格式支持字符串直接比较
- 无需解析时间戳为 time_t

### 5.4 关键词匹配优化

- 不区分大小写的简单转换
- 子串查找使用 std::string::find（O(n)复杂度）
- 未来可升级为 Boyer-Moore 算法

### 5.5 性能指标（估算）

| 操作 | 归档条数 | 平均耗时 | 内存占用 |
|-----|---------|---------|---------|
| 关键词搜索 | 1000 条 | 50 ms | 512 bytes |
| 时间范围查询 | 1000 条 | 30 ms | 512 bytes |
| 最近N条 | 1000 条 | 100 ms | ~50 KB |

---

## 六、测试验证

### 6.1 单元测试

#### 测试 1：关键词搜索

**前提**：已有归档数据包含关键词"北京"

**请求**：
```python
memory(action='recall', type='fact', keyword='北京')
```

**预期日志**：
```
I (xxx) MemMcpTools: Recalling by keyword: type=fact, keyword='北京', limit=10
I (xxx) MemArchive: Recalling fact by keyword: '北京' (limit: 10)
I (xxx) MemArchive: Recalled 2/10 items (scanned 25 lines)
I (xxx) MemMcpTools: Recalled 2 fact items from archive
```

**预期返回**：包含"北京"的 Facts

#### 测试 2：时间范围查询

**请求**：
```python
memory(action='recall', type='moment', start_date='2025-01-01', end_date='2025-12-31')
```

**预期日志**：
```
I (xxx) MemMcpTools: Recalling by time range: type=moment, start='2025-01-01', end='2025-12-31', limit=10
I (xxx) MemArchive: Recalling moment by time range: 2025-01-01 to 2025-12-31 (limit: 10)
I (xxx) MemArchive: Recalled 5/10 items (scanned 30 lines)
```

**预期返回**：2025年的 Moments

#### 测试 3：最近N条

**请求**：
```python
memory(action='recall', type='fact', limit=5)
```

**预期日志**：
```
I (xxx) MemMcpTools: Recalling recent: type=fact, limit=5
I (xxx) MemArchive: Recalling 5 most recent fact items
I (xxx) MemArchive: Recalled 5 recent items (total archived: 25)
```

**预期返回**：最近归档的 5 条 Facts

### 6.2 集成测试

**测试场景**：模拟用户对话

**用户**："你还记得我去北京的事吗？"

**年糕行为**：
1. 调用 `memory(action='recall', type='fact', keyword='北京')`
2. 检索到相关记忆
3. 自然地回答："记得呀，你喜欢吃北京烤鸭，还在北京工作~"

### 6.3 压力测试

**场景**：归档 1000 条 Facts，检索性能

**操作**：
```python
memory(action='recall', type='fact', keyword='测试')  # 100次
```

**预期**：
- 平均响应时间 < 100ms
- 内存占用 < 1KB
- 无崩溃、无内存泄漏

---

## 七、已知限制

### 7.1 单行长度限制

- JSONL 单行最大 512 字节
- 超出部分会被截断
- 建议：Fact/Moment 内容控制在 200 字符以内

### 7.2 无事务性

- 归档过程中断电可能导致部分数据丢失
- SPIFFS 不支持事务
- 建议：定期备份归档文件

### 7.3 修改/删除不支持

- JSONL 追加格式，修改需重写整个文件
- 当前不支持删除特定归档项
- 未来可考虑添加"软删除"标记

### 7.4 关键词搜索限制

- 简单的子串匹配，不支持正则表达式
- 不支持拼音搜索、同义词
- 不支持多关键词组合（AND/OR）

---

## 八、未来扩展

### 8.1 压缩归档（Priority: High）

**需求**：3 年后归档文件可能超过 6MB
**方案**：
- 使用 gzip 压缩旧归档文件（压缩率约 70%）
- 保留最近 3 个月未压缩（快速访问）
- 旧归档压缩存储

### 8.2 分页检索（Priority: Medium）

**需求**：单次检索 1000 条记忆内存占用过高
**方案**：
- 添加 offset 参数
- 支持分页加载：`recall(..., limit=10, offset=20)`

### 8.3 模糊搜索（Priority: Low）

**需求**：提升搜索准确性
**方案**：
- 拼音搜索（如"beijing" → "北京"）
- 同义词搜索（如"音乐" ↔ "歌曲"）
- Levenshtein 距离模糊匹配

### 8.4 统计分析（Priority: Low）

**需求**：了解记忆分布
**方案**：
- 时间分布统计（按月/季度）
- 关键词频率统计（TF-IDF）
- 情感分布分析（Moments）

### 8.5 导出功能（Priority: Low）

**需求**：备份和迁移
**方案**：
- 导出为 JSON 格式
- 导出为 CSV 格式（适合表格查看）
- 支持增量导出（仅导出新增记忆）

---

## 九、编译和烧录

### 9.1 编译

```bash
cd d:\xiaozhi\xiaozhi-esp32-v2\xiaozhi-esp32-v2
idf.py build
```

### 9.2 烧录

```bash
idf.py -p COM<端口号> flash monitor
```

### 9.3 预期启动日志

```
I (xxx) MemArchive: Attempting to mount memory partition...
I (xxx) MemArchive: Memory partition mounted successfully
I (xxx) MemArchive: Memory SPIFFS: total=3072 KB, used=XX KB, available=XXXX KB
I (xxx) MemArchive: SPIFFS is ready (flat filesystem, no directories needed)
I (xxx) MemArchive: Memory archive initialized successfully
```

---

## 十、总结

### 10.1 已完成的工作

✅ **Stage 2 核心功能**：
- RecallByKeyword（关键词搜索）
- RecallByTimeRange（时间范围查询）
- RecallRecent（最近N条）

✅ **MCP 工具集成**：
- 添加 recall 操作
- 添加 4 个新参数
- 更新工具描述和示例

✅ **System Prompt 更新**：
- 添加 recall 使用说明
- 提供用户场景示例

✅ **文档完善**：
- 更新架构说明文档
- 添加完整的深层记忆归档系统章节

### 10.2 代码统计

| 文件 | 新增行数 | 修改行数 |
|-----|---------|---------|
| memory_archive.h | 6 | 4 |
| memory_archive.cc | 200+ | 10 |
| memory_mcp_tools.cc | 80+ | 15 |
| system_prompt.txt | 7 | 0 |
| 记忆系统架构说明.md | 300+ | 5 |
| **总计** | **~600** | **~35** |

### 10.3 下一步建议

**优先级排序**：

1. **✅ 完成** - Stage 2: Recall/Search 功能
2. **🔜 推荐** - Phase 5: 上下文相关性（结合 Pet 状态智能建议日程）
3. **📋 可选** - 压缩归档（3年后扩容方案）
4. **📋 可选** - 模糊搜索优化

**下一步操作**：
1. 编译并烧录固件
2. 添加测试数据，触发自动归档
3. 测试 recall 功能（关键词、时间范围、最近N条）
4. 长期运行验证稳定性

---

**实现状态**：✅ **已完成**
**测试状态**：⏳ **待测试**
**文档状态**：✅ **已完成**

**下一步**：编译、烧录、测试！

---

**最后更新**：2026-02-02
**文档版本**：v1.0
**作者**：AI Assistant (Claude Sonnet 4.5)

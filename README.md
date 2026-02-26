# 🤖 XiaoZhi ESP32 AI Pet - v2 (Beta)

> **基于 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 的实验性修改版本**

[![ESP32](https://img.shields.io/badge/ESP32-S3%20|%20C3%20|%20P4-blue.svg)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Version](https://img.shields.io/badge/Version-v2%20Beta-orange.svg)]()

## ⚠️ 重要说明

**v2 版本与 v1 不兼容！** 使用了全新的分区表，无法通过 OTA 从 v1 升级。

## 🔧 主要改动

本版本在原项目基础上进行了以下实验性修改：

- 🆕 **新分区表** - 优化 Flash 布局（与 v1 不兼容）
- 🧠 **内存优化** - 改进大模型支持
- 💾 **存储扩展** - SPIFFS 从 1MB 增至 2MB
- 🐾 **宠物记忆系统** - 增强记忆功能（测试中）
- ⚡ **性能改进** - MCP 协议优化

## 📚 文档

详细使用说明请参考原项目文档：
👉 [原项目 README](https://github.com/78/xiaozhi-esp32)

本版本特定改动说明：
- 分区表变更：[partitions/v2/README.md](partitions/v2/README.md)
- 内存优化说明：`内存优化说明.md`
- 记忆系统架构：`记忆系统架构说明.md`

## 🚀 快速开始

```bash
# 克隆仓库
git clone https://github.com/haoyu9189-code/xiaozhi-esp32-pet-v2.git
cd xiaozhi-esp32-pet-v2

# 设置芯片
idf.py set-target esp32s3

# 构建
idf.py build

# 烧录（会擦除 v1 分区表！）
idf.py flash monitor
```

## 🔗 相关版本

- **v0 (归档)**: [xiaozhi-esp32-pet-v0](https://github.com/haoyu9189-code/xiaozhi-esp32-pet-v0) - 初始备份
- **v1 (稳定)**: [xiaozhi-esp32-pet-v1](https://github.com/haoyu9189-code/xiaozhi-esp32-pet-v1) - 生产版本

---

## 📜 归属说明

**本项目基于以下开源项目修改：**

- **原始项目**: [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
- **原作者**: [78](https://github.com/78)
- **许可证**: MIT

感谢原作者的开源贡献！ 🙏

**⭐ 如果觉得有用，请同时为 [原项目](https://github.com/78/xiaozhi-esp32) 点赞！**

---

**⚠️ 测试版本，可能存在不稳定情况。生产环境请使用 v1。**

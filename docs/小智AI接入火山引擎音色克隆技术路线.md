# 小智AI接入火山引擎音色克隆技术路线

> 来源：知乎@AI码上来 - AI实战系列

## 一、背景与目标

### 1.1 现有TTS方案延时对比

| TTS服务来源 | 模型名称 | 延迟时间(s) |
|-----------|---------|-----------|
| 阿里百炼 | CosyVoice | 0.4-0.5 |
| 火山引擎 | 大模型语音合成 | 0.3-0.4 |
| 本地部署 | FishSpeech1.5 | 2.0 |
| 本地部署 | CosyVoice2.0 | 1.3 |
| 本地部署 | SparkTTS | 0.5 |
| 本地部署 | IndexTTS | 0.4-0.5 |

### 1.2 本地音色克隆的痛点

- 依赖 GPU 推理，成本高
- 不适合项目早期阶段

### 1.3 解决方案

采用**火山引擎声音复刻2.0**，支持**流式输出**，无需本地GPU。

---

## 二、火山引擎声音复刻简介

### 2.1 底层模型

火山引擎声音复刻2.0 基于字节开源的 **MegaTTS** 模型。

### 2.2 模型输入要求

| 输入类型 | 要求 |
|---------|------|
| 参考音频 | 10-15秒，用于提取音频特征实现声音复刻 |
| 合成文本 | 不超过60秒，正常语速下小于300字 |

### 2.3 API接入两步走

1. **创建音色** - 上传参考音频
2. **调用语音合成** - 使用已创建的音色合成语音

---

## 三、接入准备

### 3.1 控制台配置

控制台地址：`console.volcengine.com`

**操作步骤：**
1. 创建应用
2. 勾选「声音复刻大模型」服务
   - 字符版 ✓
   - 并发版（暂不支持流式）

### 3.2 获取接口参数

需要获取以下 **4个关键参数**：
- `appid` - 应用ID
- `token` - Access Token
- `speaker_id` - 音色ID
- `Resource-Id` - 资源标识（固定值：`volc.megatts.voiceclone`）

### 3.3 免费额度

- 免费赠送1个音色
- 10次训练机会
- 合成5000字符

---

## 四、声音克隆API接入

### 4.1 创建音色（上传参考音频）

```python
def voice_upload():
    file_path = '1.wav'
    with open(file_path, 'rb') as audio_file:
        audio_data = audio_file.read()
    encoded_data = str(base64.b64encode(audio_data), "utf-8")
    audio_format = os.path.splitext(file_path)[1][1:]  # 获取文件扩展名

    url = "https://openspeech.bytedance.com/api/v1/mega_tts/audio/upload"
    headers = {
        "Content-Type": "application/json",
        "Authorization": "Bearer;" + 'token',
        "Resource-Id": "volc.megatts.voiceclone",
    }
    audios = [{"audio_bytes": encoded_data, "audio_format": audio_format}]
    data = {"appid": 'your_appid', "speaker_id": 'S_xxx', "audios": audios, "source": 2}

    response = requests.post(url, json=data, headers=headers)
    print(response.json())
```

### 4.2 查询音色状态

```python
def get_voice_status():
    url = "https://openspeech.bytedance.com/api/v1/mega_tts/status"
    headers = {
        "Content-Type": "application/json",
        "Authorization": "Bearer;" + 'token',
        "Resource-Id": "volc.megatts.voiceclone",
    }
    body = {"appid": 'your_appid', "speaker_id": 'S_xxx'}

    response = requests.post(url, headers=headers, json=body)
    if response.status_code == 200:
        status = response.json().get('status', 0)
        # 0: 未发现  1: 训练中  2: 训练完成  3: 训练失败  4: active
        print(f"voice status: {status}")
```

**状态返回字段：**
```json
{
    "BaseResp": {"StatusCode": 0, "StatusMessage": ""},
    "create_time": 1751449452000,
    "demo_audio": "",           // 示例音频，有过期时间
    "icl_speaker_id": "ICL_xxx", // 用于双向流式API
    "speaker_id": "S_xxx",       // 用于声音复刻API
    "status": 2
}
```

### 4.3 语音合成 - WebSocket协议

#### 协议选择

| 协议 | 流式输出 | 适用场景 |
|-----|---------|---------|
| HTTP | ❌ | 简单场景 |
| WebSocket | ✓ | 实时合成 |

#### 两种操作类型

| 操作类型 | 用途 | 场景 |
|---------|------|------|
| `submit` | 发送文本，服务端异步返回音频流 | 实时合成 |
| `query` | 提供reqid查询任务结果 | 异步任务、轮询、断点续传 |

#### 重要限制

> ⚠️ 接口**不支持双向流式**：每个WebSocket连接只能处理一次合成（一次submit）

#### 基础代码流程

```python
async with websockets.connect(api_url, extra_headers=header, ping_interval=None) as ws:
    await ws.send(full_client_request)  # 发送合成请求
    while True:
        res = await ws.recv()  # 等待服务端响应
        done = parse_response(res, file_to_save)
        if done:  # 收到最后一个音频包
            file_to_save.close()
            break
    print("\nclosing the connection...")
```

#### 多句文本合成策略

由于每个连接只能处理一次合成，多句需要：
1. 每次新建WebSocket连接
2. 合成结束后（收到最后一包音频）关闭连接
3. 新建连接进行下一次合成

---

## 五、接入小智AI

### 5.1 实现思路

小智AI对话中，LLM生成的文本是**一句一句**发送的，需要建立 `TtsSession` 会话统一处理。

#### 核心设计

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   用户/上游   │────▶│  TtsSession │────▶│ 火山WS服务端 │
└─────────────┘     └─────────────┘     └─────────────┘
```

#### 处理流程

1. **维护队列**：`write(text)` 只入队，不处理
2. **循环处理**：每次建立WS连接，合成一条，收到done后处理下一句
3. **完成通知**：所有文本合成完，触发 `'finished'` 事件

#### 时序图

```
用户/上游          TtsSession              火山WS服务端
   │                  │                        │
   │  write(text1)    │                        │
   │─────────────────▶│                        │
   │  write(text2)    │                        │
   │─────────────────▶│                        │
   │  finish()        │                        │
   │─────────────────▶│                        │
   │                  │    [队列处理 loop]       │
   │                  │                        │
   │                  │   建立连接并发送请求      │
   │                  │───────────────────────▶│
   │                  │                        │
   │                  │      返回音频数据        │
   │                  │◀───────────────────────│
   │                  │                        │
   │                  │        返回done         │
   │                  │◀───────────────────────│
   │                  │                        │
   │  emit 'audio'    │                        │
   │◀─────────────────│                        │
   │                  │                        │
   │ emit 'sentence_end'                       │
   │◀─────────────────│                        │
   │                  │                        │
   │ emit 'finished'  │                        │
   │◀─────────────────│                        │
```

### 5.2 延时实测

#### 方案一：普通WebSocket（每句新建连接）

| 指标 | 数值 |
|-----|------|
| 首包延时 | **0.6-0.8s** |
| WS建立连接耗时 | ~0.1s |

**问题**：延时过高，不适合实时对话

#### 方案二：双向流式API（推荐）

火山引擎的**双向流式API**已支持声音复刻2.0。

| 指标 | 数值 |
|-----|------|
| 首包延时 | **0.4-0.5s** |

**结论**：比没有声音克隆的TTS略慢，符合预期，可用于生产环境。

---

## 六、总结

### 技术路线图

```
┌────────────────────────────────────────────────────────────┐
│                    小智AI服务端                              │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌──────────┐    ┌──────────┐    ┌───────────────────┐    │
│  │   LLM    │───▶│TtsSession│───▶│  火山引擎声音复刻    │    │
│  │ 文本生成  │    │  队列管理  │    │  MegaTTS 2.0      │    │
│  └──────────┘    └──────────┘    └───────────────────┘    │
│                        │                   │               │
│                        │     WebSocket     │               │
│                        │   双向流式API      │               │
│                        └───────────────────┘               │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

### 最终延时对比

| 方案 | 首包延时 | 是否支持音色克隆 | 是否需要GPU |
|-----|---------|---------------|-----------|
| 火山引擎TTS（无克隆） | 0.3-0.4s | ❌ | ❌ |
| 火山引擎声音复刻2.0 | 0.4-0.5s | ✓ | ❌ |
| 本地SparkTTS | 0.5s | ✓ | ✓ |
| 本地IndexTTS | 0.4-0.5s | ✓ | ✓ |

### 推荐方案

**项目早期**：使用火山引擎声音复刻2.0（云端方案，无需GPU，成本可控）

**项目成熟**：可考虑本地部署SparkTTS/IndexTTS + 音色缓存优化

---

## 参考资料

- 火山引擎文档：[声音复刻2.0-最佳实践](https://www.volcengine.com/docs)
- 控制台地址：https://console.volcengine.com
- 相关文章：
  - 低延迟小智AI服务端搭建-TTS篇
  - 低延迟小智AI服务端搭建-本地TTS篇：FishSpeech流式推理
  - 低延迟小智AI服务端搭建-本地TTS篇：CosyVoice流式推理
  - 低延迟小智AI服务端搭建-本地TTS篇：音色缓存

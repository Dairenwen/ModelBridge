# ModelBridge — C++ 多模型统一接入框架

## 1. 项目概述

### 1.1 项目介绍

ModelBridge — C++ 多模型统一接入框架 是一个基于 C++17 开发的大语言模型接入 SDK。

项目通过统一的 LLMProvider 接口，屏蔽不同大模型服务之间的调用差异，实现云端大模型与本地大模型的统一调用，并提供工具调用、多轮会话、流式响应和历史记录持久化能力，供上层 AI 聊天应用使用。

**核心目标：**

- 掌握基于 HTTP/HTTPS 协议的大模型 API 调用。
- 使用 C++ 接入 DeepSeek、OpenAI、Gemini 等云端大模型。
- 通过 Ollama 接入本地部署的大语言模型。
- 封装统一的大模型访问接口，降低业务代码与具体模型服务的耦合。
- 实现多轮对话、会话管理和消息持久化。
- 支持同步响应与流式响应。
- 在 SDK 基础上扩展 AI 聊天应用。

### 1.2 项目主要功能

| 功能模块     | 功能描述                                 |
| ------------ | ---------------------------------------- |
| 模型配置     | 初始化模型连接，切换模型及调用参数       |
| 云端模型接入 | 调用 DeepSeek、OpenAI、Gemini 等模型 API |
| 本地模型接入 | 通过 Ollama 调用本地部署的模型           |
| 会话创建     | 按用户创建独立会话并分配唯一 ID          |
| 会话管理     | 获取会话列表、查询会话信息、删除会话     |
| 多轮对话     | 根据历史消息维护对话上下文               |
| 普通响应     | 等待模型生成完成后，一次性返回完整结果   |
| 流式响应     | 模型生成部分内容后，逐步返回给调用方     |
| 工具调用     | 注册工具，执行模型请求的工具并回传结果   |
| 数据持久化   | 使用 SQLite 保存会话及历史消息           |
| 日志管理     | 通过 spdlog 记录运行信息与错误           |

---

## 2. 项目整体架构

### 2.1 分层设计

项目采用分层设计，主要由调用方、SDK 核心模块、数据存储层和模型服务层构成。

```mermaid
flowchart TD
    A["调用方（命令行 / 业务应用）"]
    C["ModelBridge SDK"]

    D["LLMProvider"]
    E["SessionManager"]
    F["DataManager"]

    H["OpenAICompatibleProvider"]
    I["GeminiProvider"]
    J["OllamaProvider"]

    K["OpenAI / DeepSeek / 百炼等兼容 API"]
    M["Gemini API"]
    N["本地 Ollama 服务"]

    O[("SQLite 数据库")]

    A --> C

    C --> D
    C --> E

    D --> H
    D --> I
    D --> J

    H <-->|HTTP/HTTPS| K
    I <-->|HTTPS| M
    J <-->|HTTP| N

    D -->|绑定会话| E
    E -->|可选持久化| F
    F <--> O
```

### 2.2 核心模块职责

#### ModelBridge SDK：统一接入层

SDK 是项目的核心模块，由 LLMProvider、SessionManager 和 DataManager 配合使用。

它主要负责：

- 模型初始化与管理。
- 创建和管理会话。
- 调用指定模型。
- 管理历史聊天消息。
- 提供普通和流式消息接口。
- 协调会话数据的持久化。

#### LLMProvider：统一模型接口

负责模型连接初始化、普通和流式请求、工具执行及历史消息衔接。

不同协议的访问逻辑由对应的 Provider 子类实现，调用方通过统一接口使用。

这样可以避免业务层针对每个模型分别编写调用代码。

#### Provider：模型访问实现

Provider 负责具体模型的 API 调用。

例如：

- `OpenAICompatibleProvider`：对接 OpenAI、DeepSeek、百炼等兼容接口。
- `GeminiProvider`：对接 Gemini。
- `OllamaProvider`：对接本地 Ollama。

不同 Provider 对应不同的请求地址、认证方式、请求体及响应解析逻辑。

上述接口及三个实现类统一声明在 `SDK/include/LLMProvider.hpp` 中。

#### SessionManager：会话管理器

负责管理聊天会话及其消息记录。

主要职责：

- 创建会话。
- 根据会话 ID 查找会话。
- 获取指定用户的会话列表。
- 管理完整消息上下文，支持清空历史和恢复会话。
- 删除会话。

#### DataManager：数据管理器

负责将会话及历史消息持久化到 SQLite 数据库。

通过构造参数注入 SessionManager 后启用持久化，使用事务保存完整消息链，程序重启后可按原会话 ID 恢复历史；未注入时使用内存模式。

## 3. 核心技术栈

### 3.1 第三方库

| 技术        | 功能                 | 在项目中的作用                 |
| ----------- | -------------------- | ------------------------------ |
| C++17       | 开发语言标准         | 实现 SDK 核心模块              |
| CMake       | 项目构建             | 管理静态库编译、链接与安装     |
| cpp-httplib | HTTP 客户端/服务器库 | 向模型服务发送 HTTP/HTTPS 请求 |
| JsonCpp     | JSON 处理库          | JSON 序列化和反序列化          |
| spdlog      | 日志库               | 日志记录与问题排查             |
| fmt         | 字符串格式化         | 格式化输出                     |
| SQLite      | 嵌入式数据库         | 存储会话及历史消息             |
| OpenSSL     | TLS/SSL              | 支持 HTTPS 通信                |
| GoogleTest  | C++ 测试框架         | 编写单元测试                   |
| curl        | HTTP 测试工具        | 调试模型 API                   |
| Ollama      | 本地模型运行环境     | 加载模型并提供推理接口         |
| Postman     | API 调试工具         | 测试 HTTP 请求和响应           |

## 4. 大模型 HTTP API 调用

### 4.1 基本调用流程

C++ 程序调用大模型 API 的基本过程：

```mermaid
sequenceDiagram
    participant App as C++ 应用程序
    participant HTTP as HTTP 客户端
    participant API as 模型 API
    participant LLM as 大语言模型

    App->>HTTP: 构造请求参数
    HTTP->>API: POST JSON
    API->>LLM: 提交消息
    LLM-->>API: 生成响应
    API-->>HTTP: 返回响应数据
    HTTP-->>App: 解析 JSON
```

通常包含以下步骤：

1. 确定模型 API 地址和请求方法。
2. 构造 JSON 请求体。
3. 设置 HTTP 请求头。
4. 发送 HTTP 请求。
5. 接收服务端响应。
6. 解析 JSON，提取模型文本、工具调用和协议数据。

这些步骤由 Provider 完成。绑定会话后，Provider 自动读取历史，并通过 SessionManager 保存本轮完整消息链。

### 4.2 常见消息角色

| role      | 含义                     |
| --------- | ------------------------ |
| system    | 设置模型的角色和行为要求 |
| user      | 用户输入                 |
| assistant | 模型回复或工具调用请求   |
| tool      | 工具调用执行结果         |

其中，`system`、`user` 和 `assistant` 是普通聊天中最常用的三类角色。

以下以 OpenAI 兼容协议为例；Gemini 等原生协议由对应 Provider 转换消息格式。

```json
{
  "model": "deepseek-chat",
  "messages": [
    {
      "role": "system",
      "content": "你是一名专业的 C++ 开发工程师。"
    },
    {
      "role": "user",
      "content": "解释什么是 Reactor 模型。"
    }
  ],
  "stream": false
}
```

### 4.3 API Key

云端大模型 API 通常通过 API Key 完成身份认证。

API Key 的主要作用：

- 身份验证。
- 访问授权。
- 请求次数及使用量统计。
- 调用频率限制。
- API 计费。

常见认证请求头：

```http
Authorization: Bearer YOUR_API_KEY
```

Gemini 使用 `x-goog-api-key` 请求头；无需认证的本地 Ollama 可不设置密钥。

例如：

```http
POST https://api.deepseek.com/chat/completions
Content-Type: application/json
Authorization: Bearer YOUR_DEEPSEEK_API_KEY
```

API Key 应当通过环境变量或专用的密钥管理系统提供。

例如：

```bash
export DEEPSEEK_API_KEY="你的API密钥"
```

C++ 中读取：

```cpp
const char* apiKey = std::getenv("DEEPSEEK_API_KEY");

if (apiKey == nullptr) {
    throw std::runtime_error("DEEPSEEK_API_KEY 未设置");
}
```

SDK 也可通过 `ModelConfig::apiKeyEnv` 指定环境变量名，在 `initialize()` 时读取；直接设置 `apiKey` 时优先使用该值。

## 5. ModelBridge 核心接口

### 5.1 ModelBridge 的职责

LLMProvider 负责模型调用，SessionManager 负责会话和完整历史，DataManager 负责可选的 SQLite 持久化。调用方按需组合使用。

### 5.2 ModelBridge 主要 API

| 所属类         | 接口                                                          | 功能                             |
| -------------- | ------------------------------------------------------------- | -------------------------------- |
| LLMProvider    | `initialize(config)`                                        | 初始化模型连接和调用参数         |
| LLMProvider    | `bindSession(manager, sessionId)`                           | 绑定已有会话并自动读取、保存历史 |
| LLMProvider    | `setTools(tools)`                                           | 注册工具声明及执行函数           |
| LLMProvider    | `sendMessage(message)`                                      | 发送消息并返回完整结果           |
| LLMProvider    | `sendMessageStream(message, callback)`                      | 发送消息并逐步回调文本           |
| SessionManager | `createSession(userId, modelConfigId, title, systemPrompt)` | 创建会话，后两个参数可省略       |
| SessionManager | `getSessions(userId)` / `getMessages(sessionId)`          | 查询用户会话列表或完整消息       |
| SessionManager | `restoreSession(sessionId)`                                 | 按原 ID 恢复数据库中的会话       |
| SessionManager | `clearMessages(sessionId)` / `deleteSession(sessionId)`   | 清空历史或删除会话               |
| DataManager    | `DataManager(databasePath)`                                 | 打开或创建数据库并初始化表结构   |

### 5.3 核心调用流程

创建 DataManager（可选）→ 创建 SessionManager → 创建或恢复会话 → 初始化 Provider → 绑定会话 → 注册工具（可选）→ 发送消息。

重新运行时，打开同一数据库并恢复原会话即可续聊；模型连接配置和工具仍由调用方初始化。绑定后历史自动保存，无需重复追加返回的消息。

### 5.4 普通响应与流式响应

#### 普通响应

`sendMessage()` 等待本轮完成后返回 `ChatResult`。其中 `content` 是可见回复文本，`messages` 是本轮新增的 User、Assistant 和 Tool 消息链。

#### 流式响应

`sendMessageStream()` 通过回调返回文本增量，保存本轮历史后调用一次 `callback("", true)`，并返回同样的 `ChatResult`。该方法阻塞执行，回调在调用线程中执行。

## 6. 开发环境与项目构建

### 6.1 开发环境

项目使用 C++17 和 CMake 3.16 及以上版本构建，推荐使用 Linux 开发环境和支持 Remote SSH 的 IDE。

常用开发工具：

- VS Code 等 C++ IDE。
- CMake Tools：管理 CMake 项目构建。
- Git：版本控制。
- Postman：HTTP API 调试。
- Docker：隔离开发和运行环境。

### 6.2 Ubuntu / Docker 环境依赖安装

以下针对 Ubuntu 开发容器整理。

```bash
apt update

apt install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    curl \
    libspdlog-dev \
    libfmt-dev \
    libjsoncpp-dev \
    libgtest-dev \
    libssl-dev \
    libsqlite3-dev
```

其中：

- `libfmt-dev` 提供 fmt 开发头文件及库。
- `libsqlite3-dev` 提供 SQLite 开发文件。
- `libssl-dev` 提供 OpenSSL 开发文件。
- `build-essential` 提供常用 C/C++ 编译工具。
- `libgtest-dev` 用于构建测试，关闭 `BUILD_TESTING` 时不需要。

### 6.3 cpp-httplib 安装

cpp-httplib 是轻量级 C++ HTTP 库，常用的单头文件版本可以直接通过 `httplib.h` 使用。

克隆源码：

```bash
git clone --branch v0.61.0 --depth 1 https://github.com/yhirose/cpp-httplib.git
```

进入目录：

```bash
cd cpp-httplib
```

安装头文件：

```bash
cp httplib.h /usr/local/include/
```

使用：

```cpp
#include <httplib.h>
```

也可以将固定版本的 `httplib.h` 放入项目的第三方依赖目录，避免依赖系统全局安装。

### 6.4 编译与安装

在项目根目录执行：

```bash
cmake -S SDK -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j
sudo make -C build install
```

默认安装静态库、公开头文件及 CMake 包配置到 `/usr/local`。其他项目通过以下方式链接：

```cmake
find_package(ModelBridge CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ModelBridge::modelbridge_sdk)
```

当前预编译包适用于 Ubuntu 24.04 ARM64，其他 Linux 架构可从源码编译。发布包安装和完整示例见 [Release 使用手册](Release/README.md)。

## 7. 使用 Postman 调试本地 Ollama

### 7.1 Ollama 简介

Ollama 是一个用于运行和管理大语言模型的工具。

它支持在本地加载模型，并通过 HTTP API 向外提供模型推理服务。

默认 API 地址：

```text
http://localhost:11434
```

其中：

| 组成部分          | 含义                |
| ----------------- | ------------------- |
| `http`          | HTTP 协议           |
| `localhost`     | 本机                |
| `11434`         | Ollama 默认服务端口 |
| `/api/chat`     | 聊天接口            |
| `/api/generate` | 文本生成接口        |
| `/api/tags`     | 模型列表接口        |

官方文档：https://docs.ollama.com/api/introduction

### 7.2 准备 Ollama 环境

#### 第一步：检查 Ollama 是否安装

打开终端：

```bash
ollama --version
```

如果已经安装，会显示版本信息。

#### 第二步：查看本地模型

```bash
ollama list
```

示例：

```text
NAME                  ID              SIZE
deepseek-r1:1.5b       xxxxxxxxxxxx    1.1 GB
```

实际模型列表以本机输出为准。

#### 第三步：下载模型

如果尚未安装：

```bash
ollama pull deepseek-r1:1.5b
```

下载完成后，再次执行：

```bash
ollama list
```

确认模型存在。

#### 第四步：启动 Ollama 服务

如果 Ollama 服务尚未运行：

```bash
ollama serve
```

**注意：** 在 macOS 上使用 Ollama 桌面应用时，后台服务可能已经启动。

如果 `ollama serve` 提示端口被占用，先检查现有服务是否正常，不需要重复启动。

#### 第五步：检查 HTTP 服务

```bash
curl http://localhost:11434/api/tags
```

如果返回模型列表，说明 Ollama HTTP API 可以正常访问。

### 7.3 配置 Postman 环境

#### 第一步：打开 Postman

建议使用 Postman Desktop。

如果使用 Postman 网页版，需要选择运行在本机的 Desktop Agent，而不是通过无法直接访问本机服务的云端代理发送请求。[Postman Docs](https://learning.postman.com/latest-v-12/docs/getting-started/basics/about-postman-agent?utm_source=chatgpt.com)

#### 第二步：创建 Collection

创建一个新的 Collection：

```text
Ollama Local API
```

建议保存以下请求：

```text
Ollama Local API
│
├── GET Models
├── Chat - Non Streaming
├── Chat - Streaming
├── Chat - Multi Turn
├── Generate Text
└── OpenAI Compatible Chat
```

#### 第三步：创建 Environment

创建环境：

```text
Local Ollama
```

添加环境变量：

| Variable            | Value                                 |
| ------------------- | ------------------------------------- |
| `ollama_base_url` | `http://127.0.0.1:11434`            |
| `ollama_model`    | **qwen3-vl:8b-instruct-q4_K_M** |

### 7.4 请求一：调用本地模型进行普通聊天

[Ollama](https://docs.ollama.com/api/chat) 提供：

```http
POST /api/chat
```

用于向模型发送聊天消息。

#### 第一步：配置 HTTP 请求

在 Postman 中创建新请求。

**Method：**

```http
POST
```

**URL：**

```text
{{ollama_base_url}}/api/chat
```

#### 第二步：配置 Authorization

选择：

```text
No Auth
```

#### 第三步：配置 Headers

在 Headers 中添加：

| Key          | Value            |
| ------------ | ---------------- |
| Content-Type | application/json |
| Accept       | application/json |

#### 第四步：配置 Body

进入：

```text
Body → raw → JSON
```

输入以下完整请求体：

```json
{
  "model": "{{ollama_model}}",
  "messages": [
    {
      "role": "system",
      "content": "你是一名专业的C++后端开发工程师，擅长Linux网络编程。请使用简洁准确的语言回答问题。"
    },
    {
      "role": "user",
      "content": "请介绍一下Reactor网络模型。"
    }
  ],
  "stream": false,
  "options": {
    "temperature": 0.7,
    "num_predict": 512,
    "num_ctx": 2048
  },
  "keep_alive": "5m"
}
```

#### 第五步：查看响应

成功时通常返回：

```http
HTTP/1.1 200 OK
Content-Type: application/json
```

以下是经过简化的示意响应：

```json
{
  "model": "deepseek-r1:1.5b",
  "created_at": "2026-10-17T10:00:00Z",
  "message": {
    "role": "assistant",
    "content": "Reactor是一种事件驱动的网络服务器设计模式，通常通过事件循环和I/O多路复用机制处理网络事件。"
  },
  "done": true,
  "done_reason": "stop",
  "total_duration": 1800000000,
  "load_duration": 100000000,
  "prompt_eval_count": 58,
  "prompt_eval_duration": 120000000,
  "eval_count": 50,
  "eval_duration": 1500000000
}
```

实际回复文本、时间、Token 数量和其他字段因模型与 Ollama 版本而异。

### 7.5 请求参数详解

刚才的请求主要包含以下字段。

#### model

```json
"model": "deepseek-r1:1.5b"
```

指定需要使用的模型。

模型名称必须与本地已安装模型的名称匹配。

#### messages

```json
"messages": [
  {
    "role": "system",
    "content": "你是一名C++开发工程师"
  },
  {
    "role": "user",
    "content": "什么是Reactor模型？"
  }
]
```

`messages` 是一个数组，包含聊天消息及其角色。

#### stream

```json
"stream": false
```

控制模型是否使用流式输出。

- `false`：生成完成后一次性返回。
- `true`：生成过程中逐步返回。

Ollama 原生聊天接口默认支持流式响应。

#### options

```json
"options": {
  "temperature": 0.7,
  "num_predict": 512,
  "num_ctx": 2048
}
```

用于配置模型推理参数。

| 参数            | 含义                 |
| --------------- | -------------------- |
| `temperature` | 控制生成结果的随机性 |
| `num_predict` | 限制生成 Token 数量  |
| `num_ctx`     | 设置上下文窗口大小   |

其中：

**temperature**

```json
"temperature": 0.7
```

数值较低时，模型输出通常更加稳定。

数值较高时，模型输出通常更具随机性。

**num_predict**

```json
"num_predict": 512
```

限制本次模型生成的 Token 数量。

**num_ctx**

```json
"num_ctx": 2048
```

设置模型使用的上下文窗口大小。

#### keep_alive

```json
"keep_alive": "5m"
```

控制模型在请求结束后继续保留在内存中的时间。

例如：

```json
"keep_alive": "5m"
```

表示保持约五分钟。

这一参数可以减少连续调用时重复加载模型的开销。

### 7.6 响应字段详解

主要响应字段如下：

| 字段                     | 含义            |
| ------------------------ | --------------- |
| `model`                | 实际使用的模型  |
| `created_at`           | 响应生成时间    |
| `message.role`         | 回复角色        |
| `message.content`      | 模型生成的文本  |
| `done`                 | 是否生成完成    |
| `done_reason`          | 生成结束原因    |
| `total_duration`       | 请求总耗时      |
| `load_duration`        | 模型加载耗时    |
| `prompt_eval_count`    | 输入 Token 数量 |
| `prompt_eval_duration` | 输入处理耗时    |
| `eval_count`           | 输出 Token 数量 |
| `eval_duration`        | 输出生成耗时    |

其中，时长字段通常以**纳秒**表示。

例如：

```json
"total_duration": 1800000000
```

表示大约 1.8 秒。

**最重要的字段是：**

```json
"message": {
  "role": "assistant",
  "content": "模型生成的回复内容"
}
```

因为开发聊天系统时，通常需要提取：

```text
message.content
```

并返回给客户端。

### 7.7 请求二：多轮对话

调用普通 `/api/chat` 接口时，多轮上下文由请求消息数组承载。

例如，第一轮对话：

```text
user: 我正在学习C++的Reactor网络模型。

assistant: Reactor是一种事件驱动的网络服务器设计模式。
```

接着用户提问：

```text
user: 那么它与Proactor有什么区别？
```

如果希望模型理解“它”代表 Reactor，就需要提供之前的聊天记录。

在 Postman 中：

**Method：**

```http
POST
```

**URL：**

```text
{{ollama_base_url}}/api/chat
```

**Body：**

```json
{
  "model": "{{ollama_model}}",
  "messages": [
    {
      "role": "system",
      "content": "你是一名专业的Linux C++网络开发工程师。"
    },
    {
      "role": "user",
      "content": "我正在学习C++的Reactor网络模型。"
    },
    {
      "role": "assistant",
      "content": "Reactor是一种事件驱动的网络服务器设计模式。"
    },
    {
      "role": "user",
      "content": "那么它与Proactor有什么区别？"
    }
  ],
  "stream": false
}
```

这里的历史 `assistant` 消息应使用上一轮真实返回的内容；示例中为了简洁进行了缩写。

### 7.8 请求三：流式响应

普通请求：

```json
"stream": false
```

会在模型生成完成后返回完整响应。

但在实际聊天应用中，如果模型需要较长时间才能生成回答，用户就需要等待较久。

因此可以使用流式响应。

**Method：**

```http
POST
```

**URL：**

```text
{{ollama_base_url}}/api/chat
```

**Headers：**

```http
Content-Type: application/json
```

**Body：**

```json
{
  "model": "{{ollama_model}}",
  "messages": [
    {
      "role": "user",
      "content": "请详细解释Linux中的epoll工作流程。"
    }
  ],
  "stream": true
}
```

点击 `Send`。

#### 流式响应的结构

Ollama 原生 API 使用 **NDJSON（Newline Delimited JSON）** 输出流式数据，而不是课程中 DeepSeek 云端示例采用的 SSE `data:` 格式。

每一行是一个独立的 JSON 对象。

示意：

```json
{"model":"deepseek-r1:1.5b","message":{"role":"assistant","content":"epoll"},"done":false}
{"model":"deepseek-r1:1.5b","message":{"role":"assistant","content":"是Linux"},"done":false}
{"model":"deepseek-r1:1.5b","message":{"role":"assistant","content":"提供的I/O多路复用机制。"},"done":false}
{"model":"deepseek-r1:1.5b","message":{"role":"assistant","content":""},"done":true}
```

以上仅为展示增量响应结构的示例，实际返回还可能包含其他字段。

其中：

```json
"done": false
```

说明生成过程尚未结束。

```json
"done": true
```

说明本次响应已经结束。

处理流式响应时，需要按行解析 JSON，并根据 `message.content` 逐步组织最终文本。

注意，NDJSON 整体并不是一个普通 JSON 对象，也不是 JSON 数组，因此不能直接对整个流执行一次普通 JSON 解析。

### 7.9 请求四：使用 Generate 接口

除了 `/api/chat`，Ollama 还提供：

```http
POST /api/generate
```

它更适合直接根据 Prompt 生成文本。

**Method：**

```http
POST
```

**URL：**

```text
{{ollama_base_url}}/api/generate
```

**Body：**

```json
{
  "model": "{{ollama_model}}",
  "prompt": "请使用100字介绍C++语言的特点。",
  "stream": false,
  "options": {
    "temperature": 0.5,
    "num_predict": 256
  }
}
```

#### 响应示例

```json
{
  "model": "deepseek-r1:1.5b",
  "response": "C++是一种通用编程语言，支持面向对象、泛型编程和底层系统开发。",
  "done": true
}
```

这是简化响应。

这里需要特别注意：

**`/api/generate` 的生成结果位于 `response` 字段，而 `/api/chat` 的生成结果主要位于 `message.content` 字段。**

| 接口              | 主要输入     | 主要输出            |
| ----------------- | ------------ | ------------------- |
| `/api/chat`     | `messages` | `message.content` |
| `/api/generate` | `prompt`   | `response`        |

两者不可直接混用响应解析逻辑。

### 7.10 请求五：OpenAI 兼容接口

Ollama 还支持部分 OpenAI 兼容 API。

这一点对于后续设计统一 Provider 很有价值。

原生 Ollama 接口：

```text
http://localhost:11434/api/chat
```

OpenAI 兼容接口：

```text
http://localhost:11434/v1/chat/completions
```

在 Postman 中创建一个新的请求。

**Method：**

```http
POST
```

**URL：**

```text
{{ollama_base_url}}/v1/chat/completions
```

**Headers：**

```http
Content-Type: application/json
```

**Body：**

```json
{
  "model": "{{ollama_model}}",
  "messages": [
    {
      "role": "system",
      "content": "你是一名专业的C++工程师。"
    },
    {
      "role": "user",
      "content": "你好，请介绍一下自己。"
    }
  ],
  "stream": false,
  "temperature": 0.7,
  "max_tokens": 256
}
```

#### 响应结构

OpenAI 兼容接口的响应结构与 Ollama 原生接口不同。

示意：

```json
{
  "choices": [
    {
      "index": 0,
      "message": {
        "role": "assistant",
        "content": "你好，我是一名AI助手。"
      },
      "finish_reason": "stop"
    }
  ]
}
```

因此，模型回答位于：

```text
choices[0].message.content
```

注意：

- 原生 `/api/chat` 使用 `options.temperature` 等参数结构。
- OpenAI 兼容接口使用 `temperature`、`max_tokens` 等兼容参数。
- 兼容接口并不代表支持 OpenAI API 的全部功能。

## 8. LLMProvider 封装

对应 `SDK/include/LLMProvider.hpp`、`SDK/src/LLMProvider.cpp` 和 `SessionManager` 的当前实现。
三种协议均通过 HTTP `POST` 发送 JSON，但请求路径、认证头、消息结构和流式格式不同。
`baseUrl` 是服务根地址，`requestUrl` 是最终请求地址：

```text
requestUrl = baseUrl（去掉末尾 /）+ Provider 生成的路径
```

`sendMessage` 和 `sendMessageStream` 均返回 `ChatResult`：`content` 为本轮可见文本，`messages` 为本轮新增的 User / Assistant / Tool 消息链。Provider 自动携带历史、执行通过 `setTools` 注册的工具，并保存完整消息链；未绑定会话时使用 Provider 的本地历史；调用 `bindSession` 绑定已有会话后，改由 `SessionManager` 读取和保存该会话的历史。

### 8.1 baseUrl 与请求 URL

| 协议 / 服务                  | baseUrl 示例                                          | 普通请求路径                        | 流式请求路径                                      |
| ---------------------------- | ----------------------------------------------------- | ----------------------------------- | ------------------------------------------------- |
| OpenAI 兼容 / OpenAI         | `https://api.openai.com/v1`                         | `/chat/completions`               | `/chat/completions`                             |
| OpenAI 兼容 / DeepSeek       | `https://api.deepseek.com`                          | `/chat/completions`               | `/chat/completions`                             |
| OpenAI 兼容 / 百炼北京区域   | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `/chat/completions`               | `/chat/completions`                             |
| OpenAI 兼容 / Responses 回退 | 与当前兼容服务相同                                    | `/responses`                      | `/responses`                                    |
| Gemini 原生                  | `https://generativelanguage.googleapis.com/v1beta`  | `/models/{model}:generateContent` | `/models/{model}:streamGenerateContent?alt=sse` |
| Ollama 原生                  | `http://127.0.0.1:11434`                            | `/api/chat`                       | `/api/chat`                                     |

依据：[OpenAI Chat Completions](https://developers.openai.com/api/reference/resources/chat)、[DeepSeek 接入说明](https://api-docs.deepseek.com/)、[百炼 OpenAI 兼容接口](https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope)。

**推理深度配置**

在 `ModelConfig` 的生成参数处保留两个可选字段；未设置时不发送对应参数，使用服务端默认值。下表中的兼容协议映射以 Chat Completions 为例：

| 配置字段            | 类型                           | 含义                                             |
| ------------------- | ------------------------------ | ------------------------------------------------ |
| `reasoningEffort` | `std::optional<std::string>` | 推理级别，按协议转换或原样传递；                 |
| `thinkingEnabled` | `std::optional<bool>`        | 显式开启或关闭思考，用于 DeepSeek、百炼和 Ollama |

| 协议 / 平台              | `reasoningEffort` 映射                                    | `thinkingEnabled` 映射                           |
| ------------------------ | ----------------------------------------------------------- | -------------------------------------------------- |
| OpenAI / Custom 兼容服务 | 顶层 `reasoning_effort`                                   | 不支持，改用模型支持的推理级别                     |
| DeepSeek 兼容服务        | 顶层 `reasoning_effort`                                   | 顶层 `thinking.type`：`enabled` / `disabled` |
| 百炼兼容服务             | 顶层 `reasoning_effort`，仅适用支持该参数的模型           | 顶层 `enable_thinking`                           |
| Gemini 原生              | `generationConfig.thinkingConfig.thinkingLevel`，转为大写 | 不支持                                             |
| Ollama 原生              | 顶层 `think` 字符串                                       | 顶层 `think` 布尔值                              |

例如，配置一个支持 `high` 的 OpenAI 推理模型：

```cpp
modelBridge::ModelConfig config{};
config.configId = "default-model";
config.model.protocol = modelBridge::ApiProtocol::OpenAICompatible;
config.model.provider = modelBridge::ProviderType::OpenAI;
config.model.modelName = "your-reasoning-model-id"; // 替换为实际支持推理的模型。
config.baseUrl = "https://api.openai.com/v1";
config.apiKeyEnv = "OPENAI_API_KEY";
config.timeoutSeconds = 120;
config.reasoningEffort = "high";
// temperature 保持未设置，部分推理模型不接受它。

modelBridge::OpenAICompatibleProvider provider;
if (!provider.initialize(config))
    throw std::runtime_error(provider.getLastError());
```

需要按会话 ID 续聊时，先创建会话，再绑定到已初始化的 Provider：

```cpp
#include "SessionManager.hpp"
#include <memory>

auto sessions = std::make_shared<modelBridge::SessionManager>();
auto sessionId = sessions->createSession("user-1", config.configId, "新会话", "你是一名 C++ 开发工程师。");
if (!provider.bindSession(sessions, sessionId))
    throw std::runtime_error("会话不存在");
auto result = provider.sendMessage("解释 Reactor 模型。");
auto messages = sessions->getMessages(sessionId); // 包含系统消息、工具调用及结果。
```

新 Provider 仍需初始化，需要执行工具时另行调用 `setTools` 注册。同一 Provider 可并发发送，每轮使用开始时的配置、工具和历史快照，整条消息链按提交顺序保存；依赖上一轮回复时须等待其完成。切换模型或会话后，在途请求仍保存到原绑定会话，原会话被清空或删除后不再写回。未绑定时，重新初始化会清空本地历史。

### 8.2 OpenAI 兼容协议

OpenAI、DeepSeek、百炼以及兼容此协议的中转服务可以共用该实现。默认使用 Chat Completions；仅在 HTTP 400 / 404 / 405 且错误码为 `protocol_not_supported`、`unsupported_protocol` 或 `unsupported_endpoint` 时，回退到 `/responses`，以下表格和示例以 Chat Completions 为主。

**请求头**

| 请求头            | 值                    | 说明                       |
| ----------------- | --------------------- | -------------------------- |
| `Content-Type`  | `application/json`  | 请求体为 JSON              |
| `Authorization` | `Bearer <API_KEY>`  | 当前 Provider 要求提供密钥 |
| `Accept`        | `application/json`  | 普通请求                   |
| `Accept`        | `text/event-stream` | 流式请求                   |

**请求体参数**

| 参数                             | 是否必填             | 当前封装的含义                                                         |
| -------------------------------- | -------------------- | ---------------------------------------------------------------------- |
| `model`                        | 是                   | `ModelInfo::modelName`，请求使用的模型 ID                            |
| `messages`                     | 是                   | 按顺序保存的完整上下文，不能为空                                       |
| `messages[].role`              | 是                   | `system`、`user`、`assistant`；工具结果使用 `tool`             |
| `messages[].content`           | 是                   | 文本内容                                                               |
| `tools`                        | 注册工具时发送       | 工具名称、说明和参数 JSON Schema，由 `setTools` 注册                 |
| `messages[].tool_calls`        | Assistant 调用工具时 | 调用 ID、工具名称及参数，对应 `Message::toolCalls`                   |
| `messages[].reasoning_content` | 有思考内容时         | 对应 `Message::reasoningContent`                                     |
| `messages[].tool_call_id`      | Tool 消息需要        | 对应 `Message::toolCallId`，关联原工具调用                           |
| `stream`                       | Provider 总是发送    | `sendMessage` 为 `false`，`sendMessageStream` 为 `true`        |
| `temperature`                  | 否                   | 配置了 `temperature` 才发送，包括显式的 `0`                        |
| `max_completion_tokens`        | 否                   | `provider == ProviderType::OpenAI` 时使用的输出上限字段              |
| `max_tokens`                   | 否                   | 当前封装对其他兼容平台使用的输出上限字段                               |
| `reasoning_effort`             | 否                   | `reasoningEffort`；可用级别以具体模型为准                            |
| `thinking.type`                | 否                   | DeepSeek 的 `thinkingEnabled` 映射，值为 `enabled` 或 `disabled` |
| `enable_thinking`              | 否                   | 百炼的 `thinkingEnabled` 映射，布尔值                                |

两种 Token 上限字段只发送一种；部分模型不支持温度等采样参数，可保持未设置。
以下为 DeepSeek 等传统兼容接口的示例请求体：

```json
{
  "model": "your-model-id",
  "messages": [
    {"role": "system", "content": "你是一名 C++ 开发工程师。"},
    {"role": "user", "content": "解释 Reactor 模型。"}
  ],
  "stream": false,
  "temperature": 0.7,
  "max_tokens": 512
}
```

**普通响应:**

```json
{
  "id": "response-id",
  "model": "your-model-id",
  "choices": [
    {
      "index": 0,
      "message": {"role": "assistant", "content": "Reactor 是一种事件驱动模型。"},
      "finish_reason": "stop"
    }
  ],
  "usage": {"prompt_tokens": 20, "completion_tokens": 15, "total_tokens": 35}
}
```

| 响应参数                                 | 含义                                                        | 当前 SDK 的处理                                           |
| ---------------------------------------- | ----------------------------------------------------------- | --------------------------------------------------------- |
| `choices[0].message.content`           | 第一个候选回答的文本                                        | 汇总到 `ChatResult::content`，并保存在 Assistant 消息中 |
| `choices[0].message.tool_calls`        | 模型请求的工具调用                                          | 保存到 `Message::toolCalls`，执行工具后继续请求         |
| `choices[0].message.reasoning_content` | 模型返回的思考内容                                          | 保存到 `Message::reasoningContent`，不混入可见文本      |
| `choices[].finish_reason`              | 如 `stop`、`length`、`tool_calls`、`content_filter` | 文本长度截断保留已有内容；工具调用被截断或内容拦截时报错  |
| `usage`                                | 输入、输出及总 Token 统计                                   | 当前文本接口不返回统计                                    |
| `id`、`model`                        | 服务端响应 ID、实际模型                                     | 不作为回复文本返回                                        |

**流式响应：SSE**

请求仍发往 `/chat/completions`，只需将 `stream` 设为 `true`。响应示意：

```text
data: {"choices":[{"delta":{"role":"assistant","content":"Reactor"},"finish_reason":null}]}

data: {"choices":[{"delta":{"content":" 是一种事件驱动模型。"},"finish_reason":null}]}

data: {"choices":[{"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

可见文本位于 `choices[0].delta.content`，按增量拼接。角色事件或用量事件可以没有文本，不能因此判断响应结束。当前 Chat Completions 流要求收到可接受的 `finish_reason` 和 `[DONE]`，并正常完成 HTTP 传输。

回退到 Responses 后，历史改放在 `input` 中，输出上限使用 `max_output_tokens`，推理级别使用 `reasoning.effort`；工具调用和结果分别转换为 `function_call`、`function_call_output`。请求设置 `store: false` 并请求 `reasoning.encrypted_content`，原生 `output` 保存在 `Message::protocolData` 中供后续回放。流式读取 `response.output_text.delta`，以最终响应状态判断完成，不依赖 `[DONE]`。

**推理与历史会话注意事项**

- OpenAI 的推理级别取决于模型，例如 `low`、`medium`、`high`；不能认为所有模型均支持 `none`、`minimal`、`xhigh` 或 `max`。
- DeepSeek 当前接口支持 `reasoning_effort`（如 `none`、`low`、`high`、`max`）和 `thinking.type`。可只设置一个；同时设置时必须一致，例如 `thinkingEnabled = false` 不能与 `reasoningEffort = "high"` 同用。旧版模型或兼容实现可能只支持其中部分参数。
- 百炼使用 `thinkingEnabled = true` 开启思考；部分模型另支持 `reasoning_effort`，不能把它当作所有百炼模型的通用参数。显式关闭思考时，应避免同时设置非 `none` 的级别。部分思考模型只支持流式调用，应使用 `sendMessageStream`。
- Chat Completions 每次需要完整 `messages`；Provider 会自动携带本地或绑定会话的历史，无需调用方重建上下文。
- 未绑定时，`setSystemPrompt` 设置每轮使用的系统提示；绑定后使用会话中的 System 消息，`setSystemPrompt` 同步更新会话。服务端缓存不替代 SDK 的历史管理。
- 成功后，SDK 自动保存本轮 User / Assistant / Tool 消息链；流式增量只用于展示。后续请求失败时保留已完成的工具步骤，首个请求失败时不保存孤立的 User 消息。
- `getMessages(sessionId)` 返回完整历史，可查询工具名称、参数、执行结果、思考内容及消息中记录的模型和思考配置；工具执行函数需由调用方注册。已有消息 ID 用于保存去重，不要只保存 `ChatResult::content` 来恢复工具会话。

依据：[OpenAI 推理参数](https://developers.openai.com/api/docs/guides/reasoning?api-mode=chat)、[DeepSeek 请求参数](https://api-docs.deepseek.com/api/create-chat-completion/)、[百炼深度思考](https://help.aliyun.com/zh/model-studio/deep-thinking)、[DeepSeek 多轮对话](https://api-docs.deepseek.com/guides/multi_round_chat/)、[OpenAI 会话状态](https://developers.openai.com/api/docs/guides/conversation-state)。

### 8.3 Gemini 原生协议

对应 `GeminiProvider` 和 `ApiProtocol::Gemini`。

**请求地址及请求头**

```text
普通：POST https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent
流式：POST https://generativelanguage.googleapis.com/v1beta/models/{model}:streamGenerateContent?alt=sse
```

模型名可配置为 `your-model-id` 或 `models/your-model-id`；Provider 会统一处理前缀并编码路径段。

| 请求头             | 值                                                            |
| ------------------ | ------------------------------------------------------------- |
| `Content-Type`   | `application/json`                                          |
| `x-goog-api-key` | `<GEMINI_API_KEY>`，当前封装放在请求头中，不拼进 URL        |
| `Accept`         | 普通请求为 `application/json`；流式为 `text/event-stream` |

**请求体参数**

| 参数                                              | 是否必填             | 当前封装的含义                                              |
| ------------------------------------------------- | -------------------- | ----------------------------------------------------------- |
| `contents`                                      | 是                   | 用户及模型的对话历史，至少包含一条非 System 消息            |
| `contents[].role`                               | 当前封装发送         | User 映射为 `user`，Assistant 映射为 `model`            |
| `contents[].parts[].text`                       | 文本请求需要         | 一条消息的文本内容                                          |
| `tools[].functionDeclarations`                  | 注册工具时发送       | 工具名称、说明和参数 JSON Schema                            |
| `contents[].parts[].functionCall`               | Assistant 调用工具时 | 工具调用名称、参数及可选 ID                                 |
| `contents[].parts[].functionResponse`           | Tool 消息需要        | 工具执行结果，消息角色为 `user`                           |
| `systemInstruction.parts[].text`                | 否                   | System 消息放在这里，不放入 `contents`                    |
| `generationConfig.temperature`                  | 否                   | 采样温度                                                    |
| `generationConfig.maxOutputTokens`              | 否                   | 输出 Token 上限                                             |
| `generationConfig.thinkingConfig.thinkingLevel` | 否                   | `reasoningEffort` 转为大写后发送，如 `high` → `HIGH` |

Gemini 的模型名位于 URL 中，原生请求体不添加 OpenAI 风格的 `model`、`messages` 或 `stream`。
连续同角色消息在当前实现中合并为同一条内容的多个 parts。

```json
{
  "systemInstruction": {
    "parts": [{"text": "你是一名 C++ 开发工程师。"}]
  },
  "contents": [
    {"role": "user", "parts": [{"text": "解释 Reactor 模型。"}]}
  ],
  "generationConfig": {"temperature": 0.7, "maxOutputTokens": 512}
}
```

**普通响应**

```json
{
  "candidates": [
    {
      "index": 0,
      "content": {
        "role": "model",
        "parts": [{"text": "Reactor 是一种事件驱动模型。"}]
      },
      "finishReason": "STOP"
    }
  ],
  "usageMetadata": {"promptTokenCount": 20, "candidatesTokenCount": 15, "totalTokenCount": 35}
}
```

| 响应参数                               | 含义                     | 当前 SDK 的处理                                                       |
| -------------------------------------- | ------------------------ | --------------------------------------------------------------------- |
| `candidates[0].content.parts[].text` | 第一个候选回答的文本片段 | 按顺序拼接，不包含 `thought: true` 的片段                           |
| `candidates[0].finishReason`         | 生成结束原因             | 接受 `STOP`、`MAX_TOKENS`；工具调用被截断或其他非空原因按失败处理 |
| `promptFeedback.blockReason`         | 输入被拦截的原因         | 抛出异常                                                              |
| `usageMetadata`                      | Token 使用情况           | 当前文本接口不返回统计                                                |

**流式响应：SSE**

使用 `:streamGenerateContent?alt=sse`，每个 SSE `data:` 事件仍是一个原生 JSON 对象，
文本位置仍为 `candidates[0].content.parts[].text`：

```text
data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Reactor"}]}}]}

data: {"candidates":[{"content":{"role":"model","parts":[{"text":" 是一种事件驱动模型。"}]},"finishReason":"STOP"}]}
```

Gemini 原生流不依赖 OpenAI 的 `[DONE]` 标记。当前实现要求出现可接受的 `finishReason`，
并且 HTTP 传输正常完成，才通知调用方结束。

**推理与历史会话注意事项**

- 支持级别控制的模型可设置 `reasoningEffort = "high"`，发送 `thinkingLevel: "HIGH"`；级别枚举包括 `MINIMAL`、`LOW`、`MEDIUM`、`HIGH`，具体可用项取决于模型。早期模型不支持 `thinkingLevel`。
- 当前封装仅提供推理级别设置；不支持级别控制的旧模型应保持 `reasoningEffort` 未设置，沿用服务端默认行为。`thinkingEnabled` 在此协议下会被拒绝。SDK 不主动请求输出思考摘要。
- 当前原生接口每轮通过 `contents` 传入历史和新问题；Provider 自动从本地或绑定会话中构造这些内容，本地 `sessionId` 不会发送给 Gemini。
- 每轮保留所需的 `System` 消息，Provider 将其放入独立的 `systemInstruction`；`Assistant` 转为 `model`，`User` 转为 `user`，连续同角色文本合并为多个 `parts`。不要把系统指令混作用户历史。
- SDK 自动保存本轮完整消息链；模型发起 `functionCall` 后执行已注册工具，再将结果作为 `functionResponse` 继续发送。历史长度仍受模型上下文窗口限制。
- Gemini 的 `thoughtSignature` 是不透明的协议数据，不能用普通文本代替。当前通过 `Message::protocolData` 保留原始 `parts`，包括签名和工具调用；模型与配置标识匹配且数据结构适用时原样回放，否则使用公共文本和工具字段转换。当前公共消息接口仍以文本和工具会话为主，不提供多模态输入封装。

依据：[Gemini API](https://ai.google.dev/api/generate-content)、[GenerateContent 文本生成](https://ai.google.dev/gemini-api/docs/generate-content/text-generation)、[推理级别](https://ai.google.dev/gemini-api/docs/generate-content/thinking)、[思考签名](https://ai.google.dev/gemini-api/docs/generate-content/thought-signatures)。

### 8.4 Ollama 原生协议

对应 `OllamaProvider` 和 `ApiProtocol::Ollama`，当前封装调用 `/api/chat`。

**请求地址及请求头**

```text
POST http://127.0.0.1:11434/api/chat
```

| 请求头            | 值                          | 说明                                    |
| ----------------- | --------------------------- | --------------------------------------- |
| `Content-Type`  | `application/json`        | 请求体为 JSON                           |
| `Accept`        | `application/json`        | 当前封装发送此值；流式响应实际为 NDJSON |
| `Authorization` | 可选的 `Bearer <API_KEY>` | 本地无认证服务不发送；配置了密钥才发送  |

**请求体参数**

| 参数                                        | 是否必填             | 当前封装的含义                                                                         |
| ------------------------------------------- | -------------------- | -------------------------------------------------------------------------------------- |
| `model`                                   | 是                   | 已安装的完整模型名，包含实际版本或量化标签                                             |
| `messages`                                | 是                   | 支持 System、User、Assistant、Tool 的完整历史                                          |
| `messages[].role`、`messages[].content` | 是                   | 消息角色与文本                                                                         |
| `tools`                                   | 注册工具时发送       | 工具名称、说明和参数 JSON Schema                                                       |
| `messages[].tool_calls`                   | Assistant 调用工具时 | 工具名称和参数，参数转换为 JSON 对象                                                   |
| `messages[].tool_name`                    | Tool 消息使用        | 对应 `Message::toolName`，结果保存在 `content`                                     |
| `messages[].thinking`                     | 有思考内容时         | 回传 `Message::reasoningContent`                                                     |
| `stream`                                  | Provider 总是发送    | 普通为 `false`，流式为 `true`；原生 API 的默认值是 `true`                        |
| `options.temperature`                     | 否                   | 采样温度，放在 `options` 中                                                          |
| `options.num_predict`                     | 否                   | 由 `maxOutputTokens` 映射出的输出上限                                                |
| `think`                                   | 否                   | `reasoningEffort` 字符串或 `thinkingEnabled` 布尔值；位于顶层，不在 `options` 内 |

```json
{
  "model": "your-local-model:tag",
  "messages": [{"role": "user", "content": "解释 Reactor 模型。"}],
  "stream": false,
  "options": {"temperature": 0.7, "num_predict": 512}
}
```

**普通响应**

```json
{
  "model": "your-local-model:tag",
  "message": {"role": "assistant", "content": "Reactor 是一种事件驱动模型。"},
  "done": true,
  "done_reason": "stop",
  "prompt_eval_count": 20,
  "eval_count": 15,
  "total_duration": 1000000000
}
```

| 响应参数                              | 含义                       | 当前 SDK 的处理                                           |
| ------------------------------------- | -------------------------- | --------------------------------------------------------- |
| `message.content`                   | 可见回复文本               | 汇总到 `ChatResult::content`，并保存在 Assistant 消息中 |
| `message.tool_calls`                | 模型请求的工具调用         | 保存到 `Message::toolCalls`，执行工具后继续请求         |
| `message.thinking`                  | 模型的思考内容，可能缺省   | 保存到 `Message::reasoningContent`，不混入可见回复      |
| `done`                              | 本次生成是否完成           | 普通响应必须为 `true`；流式也用它判断结束               |
| `done_reason`                       | 如自然结束或达到长度限制   | 当前返回接口不单独暴露                                    |
| `prompt_eval_count`、`eval_count` | 输入及输出 Token 数量      | 当前返回接口不单独暴露                                    |
| `total_duration` 等耗时字段         | 原生响应的计时，单位为纳秒 | 当前返回接口不单独暴露                                    |

**流式响应：NDJSON**

仍调用 `/api/chat`，将 `stream` 设为 `true`，每行是一个独立 JSON 对象：

```json
{"message":{"role":"assistant","content":"Reactor"},"done":false}
{"message":{"role":"assistant","content":" 是一种事件驱动模型。"},"done":false}
{"message":{"role":"assistant","content":""},"done":true}
```

NDJSON 没有 SSE 的 `data:` 前缀，也没有 `[DONE]`。最后一条记录可以同时含有新增文本、
结束状态和统计信息，应先处理文本，再处理结束状态。

**推理与历史会话注意事项**

- 模型支持布尔开关时，设置 `thinkingEnabled = true` 或 `false`；支持分级时，改用 `reasoningEffort`。两者不能同时设置，因为都会映射到同一个 `think` 字段。
- 字符串级别由模型定义，可通过 Ollama 的 `/api/show` 查看支持值，不能假定每个模型都接受 `low/medium/high`。未设置时不发送 `think`，沿用模型默认行为；非思考模型不能因此获得推理能力。
- 本地运行也需要每次把所需历史放进 `messages`。模型常驻内存或服务端缓存不会替应用维护这个 SDK 的会话；`keep_alive` 控制模型驻留，与聊天历史管理不同。
- Provider 自动携带 System 设定、之前的 User / Assistant / Tool 消息及新问题，并保存本轮消息链。通过 `bindSession` 可按已有会话 ID 续聊；查询历史使用 `getMessages`，无需手工拼接流式片段或重复追加回复。
- 当前保存 `message.thinking`、完整工具调用和结果，流式工具调用按完整调用追加。模型必须支持所注册的工具或思考参数；上下文仍受本地模型窗口限制，SDK 暂未自动裁剪或摘要。

依据：[Ollama Chat](https://docs.ollama.com/api/chat)、[Ollama 流式协议](https://docs.ollama.com/api/streaming)。

## 9. SessionManager 会话管理

对应 `SDK/include/SessionManager.hpp`、`SDK/src/SessionManager.cpp`
`SessionManager` 默认在内存中保存多组会话和完整消息，负责创建、查询、追加、清空及删除会话数据；构造时传入 `DataManager` 可启用 SQLite 持久化，具体用法见第 10 节。模型请求、协议转换和工具执行由 `LLMProvider` 负责。

### 9.1 会话与消息的数据结构

内部使用 `std::unordered_map<std::string, Session>`，以会话 ID 查找会话；每个 `Session` 使用 `std::vector<Message>` 按保存顺序存放消息。

**Session 会话字段**

| 字段                         | 含义                   | 当前实现的处理                                                  |
| ---------------------------- | ---------------------- | --------------------------------------------------------------- |
| `sessionId`                | 会话唯一 ID            | 创建会话时生成，后续按此 ID 查询或绑定                          |
| `userId`                   | 所属用户 ID            | 创建时记录；`getSessions(userId)` 按用户查询会话概要          |
| `modelConfigId`            | 最近使用的模型连接标识 | 创建时记录初始值，追加新消息后使用最后一条新增消息的配置 ID     |
| `title`                    | 会话标题               | 创建时传入，默认为空                                            |
| `messages`                 | 完整历史               | 包括 System、User、Assistant、Tool 消息                         |
| `createdAt`、`updatedAt` | 创建及最近活动时间     | Unix 毫秒；创建、追加消息、修改系统提示及清空消息时更新相应时间 |
| `generation`               | 会话消息版本           | 清空消息时递增，用于阻止旧请求重新写回                          |

**Message 消息字段**

| 字段                                     | 含义                     | 当前实现的处理                                             |
| ---------------------------------------- | ------------------------ | ---------------------------------------------------------- |
| `messageId`                            | 消息记录 ID              | 已有 ID 保留，用于去重；缺少时生成                         |
| `role`、`content`                    | 消息角色和文本           | 保留原始角色与内容，Tool 的 `content` 为执行结果         |
| `createdAt`                            | 消息创建时间             | 为 `0` 时补充当前时间，已有时间保留                      |
| `toolCalls`                            | Assistant 请求的工具调用 | 保留调用 ID、工具名称及 JSON 参数字符串                    |
| `toolCallId`、`toolName`             | Tool 消息的关联信息      | 保留协议提供的调用关联和工具名称，调用 ID 可能因协议而为空 |
| `reasoningContent`、`protocolData`   | 思考及原生协议数据       | 原样保存，协议转换和回放由 Provider 处理                   |
| `modelConfigId`、`modelName`         | 生成消息时的模型信息     | 保存请求时的快照，不随后续模型切换改变                     |
| `reasoningEffort`、`thinkingEnabled` | 当时的思考设置           | 保留可选值，未设置与显式关闭分别保存                       |

会话 ID 和消息 ID 分别带 `session`、`message` 前缀，由时间戳、进程内初始化一次的随机标识和原子递增序号组成。多个管理器共享生成序号，清空会话不会重置序号；消息记录 ID 与工具协议中的调用 ID 分开使用。

### 9.2 主要接口

| 接口                                                                  | 做的工作                                           | 返回值或注意事项                                                             |
| --------------------------------------------------------------------- | -------------------------------------------------- | ---------------------------------------------------------------------------- |
| `createSession(userId, modelConfigId, title, systemPrompt)`         | 创建会话；非空系统提示保存为首条 System 消息       | 返回新会话 ID，后两个参数默认为空                                            |
| `getSession(sessionId)`                                             | 查询会话及完整消息                                 | 返回 `std::optional<Session>` 快照；不存在为 `std::nullopt`              |
| `addMessage(sessionId, message, modelConfig)`                       | 追加单条消息                                       | 内部调用 `addMessages`，会话不存在返回 `false`                           |
| `addMessages(sessionId, messages, modelConfig, expectedGeneration)` | 按原顺序追加一批消息，补充缺失信息并按 ID 去重     | 会话不存在或指定版本不匹配返回 `false`；版本参数默认不检查                 |
| `getMessages(sessionId)`                                            | 查询完整消息列表                                   | 返回 `std::optional<std::vector<Message>>` 快照；不存在为 `std::nullopt` |
| `getAllSessions()`                                                  | 查询本管理器已创建或恢复的会话，按更新时间降序排列 | 返回包含完整消息的副本；持久化模式读取这些会话的最新记录                     |
| `getSessions(userId)`                                               | 查询指定用户的会话概要                             | 按更新时间降序、ID 升序排列，`messages` 为空                               |
| `restoreSession(sessionId)`                                         | 按原 ID、时间和版本恢复数据库会话到内存            | 不存在返回 `false`；内存模式只检查记录是否存在                             |
| `setSystemPrompt(sessionId, prompt)`                                | 替换会话中的 System 消息                           | 空字符串移除系统提示；会话不存在返回 `false`                               |
| `updateSessionTitle(sessionId, title)`                              | 修改标题并刷新活动时间                             | 会话不存在返回 `false`                                                     |
| `clearMessages(sessionId)`                                          | 清空指定会话的非 System 消息                       | 保留会话和系统提示，递增版本；会话不存在返回 `false`                       |
| `deleteSession(sessionId)`                                          | 删除指定会话及全部消息                             | 删除成功返回 `true`；不存在返回 `false`                                  |
| `updateSessionTimestamp(sessionId)`                                 | 显式更新最近活动时间                               | 会话不存在返回 `false`；普通查询不会更新该时间                             |
| `clearSessions()`                                                   | 清空纯内存模式的全部会话                           | 持久化模式须使用带用户 ID 的重载；不重置 ID 生成序号                         |
| `clearSessions(userId)`                                             | 清空指定用户的会话及消息                           | 同步内存和数据库，不影响其他用户                                             |
| `getSessionCount()`                                                 | 查询本管理器已创建或恢复且仍存在的会话数量         | 返回 `std::size_t`；完整用户列表使用 `getSessions(userId)`               |
| `generateSessionId()`、`generateMessageId()`                      | 单独生成 ID                                        | 静态方法，仅生成标识，不创建记录                                             |

### 9.3 与 LLMProvider 配合使用

**创建、绑定和续聊**

```cpp
#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include <memory>
#include <stdexcept>

int main()
{
    auto sessions = std::make_shared<modelBridge::SessionManager>();

    modelBridge::ModelConfig config{};
    config.configId = "local-model";
    config.model.protocol = modelBridge::ApiProtocol::Ollama;
    config.model.provider = modelBridge::ProviderType::Ollama;
    config.model.modelName = "your-local-model:tag"; // 替换为已安装的模型。
    config.baseUrl = "http://127.0.0.1:11434";
    config.timeoutSeconds = 120;

    auto sessionId = sessions->createSession("user-1", config.configId, "C++ 学习", "你是一名 C++ 开发工程师。");
    modelBridge::OllamaProvider provider;
    if (!provider.initialize(config))
        throw std::runtime_error(provider.getLastError());
    if (!provider.bindSession(sessions, sessionId))
        throw std::runtime_error("会话不存在");

    auto first = provider.sendMessage("解释 Reactor 模型。");
    auto second = provider.sendMessage("它适合哪些场景？"); // 自动携带前文。
    auto history = sessions->getMessages(sessionId); // 用于显示完整历史。
    return 0;
}
```

绑定后，每次发送都会读取该会话的当前历史；请求结束后，Provider 将本轮新增消息写回同一会话。`ChatResult::messages` 不包含此前历史，也不包含系统提示，无需在绑定模式下再次手工保存。

用户选择已有会话时，将原会话 ID 传给 `bindSession(sessions, sessionId)` 即可继续聊天。该方法属于 Provider，绑定失败返回 `false`，原绑定保持不变，调用方应检查返回值。新 Provider 仍需初始化；需要执行工具时，还需通过 `setTools` 注册工具定义和实现。

**切换模型与系统提示**

- 同一会话可以使用不同模型或思考设置。调用 `initialize(newConfig)` 切换配置后，绑定会话的历史保留；改用另一个 Provider 时，将其初始化后绑定到同一会话。
- Provider 在请求开始时取得配置快照，并给新增消息填写模型信息；管理器保留消息已有的模型信息。手工追加的消息若 `modelName` 为空，则使用传入的 `modelConfig` 补充模型与思考设置。
- 绑定后，`provider.setSystemPrompt` 同步修改该会话的 System 消息，`provider.clearHistory` 清空该会话的非 System 消息。`provider.getHistory()` 不包含 System 消息，完整展示应使用管理器的 `getMessages()`。
- 未绑定时，Provider 使用独立的本地历史；重新初始化会清空这部分历史。`bindSession(nullptr)` 解除绑定并开始空的本地历史。

### 9.4 消息保存与工具调用记录

管理器保存完整消息结构,例如，一轮工具调用的消息顺序为：

```text
User：用户问题
Assistant：工具名称、调用参数及模型返回的文本或思考内容
Tool：工具执行结果或错误内容
Assistant：结合工具结果生成的最终回复
```

通过 `getMessages(sessionId)` 或 `getSession(sessionId)` 可以查看这些记录；显示工具调用时读取 Assistant 的 `toolCalls`，显示工具结果时读取 Tool 消息的 `toolName`、`toolCallId` 和 `content`。流式文本回调不返回完整工具过程，应在消息保存后查询历史，或读取本轮 `ChatResult::messages`。

**追加与去重规则**

- `addMessage`、`addMessages` 按值接收消息，补充 ID、时间和模型信息时不修改调用方原对象。
- 已有消息 ID 保留；与已存消息或本批次前面的消息 ID 重复时跳过，不覆盖旧内容。空列表或全部重复的消息不会改变会话时间及最近模型标识。
- 手工创建的消息需要在首次提交前设置稳定 ID，才能对重复提交去重；反复提交空 ID 的消息会被视为新记录。Provider 返回的消息已带 ID，可直接用于保存去重。
- 成功追加后更新会话的 `updatedAt` 和最近模型配置 ID；系统提示单独保存，后续切换模型不会改写已存消息的模型信息。

### 9.5 并发访问与旧请求处理

内部使用一把 `std::shared_mutex` 保护会话映射和消息。查询使用读锁，新增、修改、清空和删除使用写锁；消息副本的字段准备、ID 生成及会话列表排序放在锁外。

**清空消息时的版本检查**

Provider 读取历史时同时记录 `Session::generation`，保存时通过 `expectedGeneration` 传回管理器。例如：

```text
请求开始：读取历史，记录 generation = 0
清空消息：保留 System，generation 递增为 1
请求返回：提交时版本仍为 0，与当前版本不匹配，拒绝写回
```

这可以避免刚清空的历史被在途请求恢复。删除会话后，旧请求也无法重新创建该会话。调用 `addMessages` 时，只有显式传入 `expectedGeneration` 才会进行版本检查。

- 每轮已完成的消息链在一次写锁内追加，其他线程不会读到本批次正常追加过程中的中间状态;
- 并发请求使用各自开始时的历史，尚未完成的回复对彼此不可见；有上下文先后依赖时，应等待上一轮完成再发送下一轮。
- 切换模型或绑定会话不会改变已开始请求的归属；其结果仍保存到原会话，原会话已清空或删除时不再写回。
- 调用期间入参须保持有效且不被其他线程修改；销毁管理器及 Provider 前须等待相关调用结束。

## 10. DataManager 数据持久化

对应 `SDK/include/DataManager.hpp`、`SDK/src/DataManager.cpp` 和 `SDK/schema.sql`。
`DataManager` 使用 SQLite 保存会话和完整历史消息，负责数据库连接、读写和事务；`SessionManager` 负责会话管理、ID 生成及消息信息补充，`LLMProvider` 负责模型请求、协议转换和工具执行。

启用持久化后，消息沿已有调用链保存：

```text
LLMProvider：生成本轮 User / Assistant / Tool 消息链
SessionManager：补充缺失信息，检查版本并按消息 ID 去重
DataManager：在同一个事务中追加消息、分配顺序号并更新会话信息
```

### 10.1 数据库与保存内容

构造 `DataManager` 时打开或创建数据库，并创建必要的数据表和索引；再次打开已有数据库会保留原数据。默认文件为程序当前工作目录下的 `DBFiles/modelbridge.db`，也可传入自定义路径；对象销毁时释放连接。

| 数据表 / 字段                            | 做的工作                   | 当前实现的处理                                                        |
| ---------------------------------------- | -------------------------- | --------------------------------------------------------------------- |
| `sessions`                             | 保存会话信息               | 包括 ID、所属用户、最近模型标识、标题、创建及活动时间、`generation` |
| `messages`                             | 保存完整消息               | 保存第 9.1 节列出的全部 `Message` 字段，关联所属会话                |
| `position`                             | 记录会话内的消息顺序       | 数据库内部字段，读取时按此排序，不按消息创建时间重排                  |
| `toolCalls`                            | 保存完整工具调用数组       | 使用 JSON 文本保存，保留调用顺序及原始参数字符串                      |
| `reasoningContent`、`protocolData`   | 保存思考内容和原生协议数据 | 原样保存字符串，包括签名及协议回放所需的数据                          |
| `reasoningEffort`、`thinkingEnabled` | 保存当时的思考设置         | 区分未设置、空字符串及显式 `false`                                  |

同一会话内的 `messageId` 唯一，重复提交跳过已存消息，不覆盖旧内容。消息通过外键关联会话，删除会话时一并删除其消息。连接配置、API Key 和工具函数实现不存入数据库，重新聊天时仍由调用方初始化和注册。

建表语句统一保存在 `SDK/schema.sql`，CMake 通过 `SDK/src/DataManagerSchema.hpp.in` 将其生成到构建目录中的头文件，再编入 SDK；程序运行时无需查找源码目录中的 SQL 文件。

### 10.2 主要接口

日常聊天通过 `SessionManager` 使用持久化。直接调用 `DataManager` 时，传入的会话和消息应已准备好 ID、时间及模型信息。

| 接口                                                    | 做的工作                         | 返回值或注意事项                                                     |
| ------------------------------------------------------- | -------------------------------- | -------------------------------------------------------------------- |
| `DataManager(databasePath)`                           | 打开数据库并建表                 | 路径为空使用默认文件；初始化失败抛出异常                             |
| `createSession(session)`                              | 保存新会话及初始消息             | 一个事务完成；重复会话 ID 抛出异常                                   |
| `updateSession(session)`                              | 更新标题、最近模型标识和活动时间 | 不覆盖历史、创建时间、所属用户或版本；不存在返回 `false`           |
| `appendMessage(...)`、`appendMessages(...)`         | 追加单条或一批完整消息           | 返回 `Success`、`NotFound` 或 `VersionMismatch`                |
| `getSession(sessionId)`、`getMessages(sessionId)`   | 读取会话或完整历史               | 不存在返回 `std::nullopt`；查询不更新活动时间                      |
| `getSessions(userId)`                                 | 查询指定用户的会话概要           | 更新时间降序、ID 升序排列，不加载消息正文                            |
| `setSystemPrompt(sessionId, prompt, updatedAt)`       | 替换 System 消息                 | `prompt` 为 `std::optional<Message>`，空值表示移除；其他历史保留 |
| `updateSessionTimestamp(sessionId, updatedAt)`        | 单独更新活动时间                 | 不存在返回 `false`                                                 |
| `clearMessages(sessionId, updatedAt)`                 | 清空非 System 消息               | 保留会话及系统提示，同时递增 `generation`                          |
| `deleteSession(sessionId)`、`clearSessions(userId)` | 删除会话及其消息                 | 批量删除须明确用户范围，不影响其他用户                               |

`NotFound` 表示会话不存在，`VersionMismatch` 表示请求版本已过期；数据库读写失败抛出异常。

### 10.3 与 SessionManager、LLMProvider 配合使用

**启用持久化并创建会话**

```cpp
#include "DataManager.hpp"
#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include <memory>
#include <stdexcept>

auto data = std::make_shared<modelBridge::DataManager>(); // 也可传入数据库文件路径。
auto sessions = std::make_shared<modelBridge::SessionManager>(data);

modelBridge::ModelConfig config{};
config.configId = "local-model";
config.model.protocol = modelBridge::ApiProtocol::Ollama;
config.model.provider = modelBridge::ProviderType::Ollama;
config.model.modelName = "your-local-model:tag"; // 替换为已安装的模型。
config.baseUrl = "http://127.0.0.1:11434";
config.timeoutSeconds = 120;

auto sessionId = sessions->createSession("user-1", config.configId, "C++ 学习", "你是一名 C++ 开发工程师。");
modelBridge::OllamaProvider provider;
if (!provider.initialize(config))
    throw std::runtime_error(provider.getLastError());
if (!provider.bindSession(sessions, sessionId))
    throw std::runtime_error("会话不存在");
auto result = provider.sendMessage("解释 Reactor 模型。"); // 本轮消息自动保存到数据库。
auto history = sessions->getMessages(sessionId); // 包含 System 和完整工具过程。
```

不传入 `DataManager` 时，`SessionManager` 保持纯内存模式。Provider 未绑定会话时仍使用自身的本地历史，不会自动保存到数据库。

**程序重启后恢复和续聊**

重新打开同一个数据库，先查询用户的会话列表，再使用用户选定的原会话 ID 恢复；不要重新创建会话来代替恢复。以下示例沿用上面的模型配置：

```cpp
auto data = std::make_shared<modelBridge::DataManager>();
auto sessions = std::make_shared<modelBridge::SessionManager>(data);
auto list = sessions->getSessions("user-1"); // 仅概要，供界面显示和选择。
std::string selectedSessionId = "已保存的会话 ID"; // 使用用户实际选定的 ID。
if (!sessions->restoreSession(selectedSessionId))
    throw std::runtime_error("会话不存在");

modelBridge::OllamaProvider provider;
if (!provider.initialize(config))
    throw std::runtime_error(provider.getLastError());
if (!provider.bindSession(sessions, selectedSessionId))
    throw std::runtime_error("会话不存在");
// 如需继续执行工具，在发送前通过 setTools 注册工具定义和实现。
auto result = provider.sendMessage("接着上次的内容，它适合哪些场景？");
```

恢复时保留原会话和消息的 ID、时间、消息顺序及版本。切换模型不会修改已存消息的模型快照，后续追加的消息使用本次请求的模型与思考设置。

### 10.4 事务并发与失败

内部使用单个 SQLite 连接和一把 `std::mutex`，覆盖一次完整数据库操作或事务；与 `SessionManager` 配合时，锁顺序固定为会话锁、数据库锁。

- 新建会话及初始消息、整轮消息追加、系统提示替换和清空历史分别使用事务；失败时回滚，避免保存半条消息链或只更新会话信息。
- 追加消息、分配顺序号、更新最近模型标识和活动时间在同一个事务中完成；空列表或全部重复消息不更新会话信息。
- `generation` 检查与消息写入在同一个事务中完成。清空或删除后，旧请求不能恢复已移除的历史；切换绑定后，请求仍保存到原会话。
- 普通及流式回复均在完整消息链保存后返回；流式的完成回调在保存之后触发。数据库保存失败会抛出异常，不发送完成通知。
- 首个请求失败时不保存孤立的 User 消息；工具执行后若后续请求失败，已经完成的消息和工具结果仍保存，未完成的响应不保存。
- 销毁对象前须等待相关调用结束。

## 项目扩展：RESTful API 设计

### 1. RESTful API 简介

RESTful API 是一种基于 HTTP 协议的接口设计风格，REST 是 Representational State Transfer（表现层状态转移）的缩写。

其核心思想是：**将系统中的数据和功能抽象为资源，通过 URL 标识资源，使用 HTTP 请求方法对资源进行操作，并通过 JSON 等格式交换数据。**

RESTful API 具有以下特点：

- **资源化设计**：使用 URL 标识资源，例如用户、会话、消息等。
- **统一操作方式**：使用 GET、POST、PUT、PATCH、DELETE 等 HTTP 方法。
- **无状态通信**：每个请求独立，包含服务器处理请求所需的信息。
- **统一数据格式**：通常使用 JSON 传递请求参数和响应结果。
- **前后端解耦**：客户端只需遵循接口规范，无需了解服务端内部实现。

### 2. RESTful API 设计方法

RESTful API 通常按照 **资源定义 → URL 设计 → HTTP 方法选择 → 请求参数设计 → 响应结果设计** 的流程进行。

**（1）定义资源与 URL**

URL 应主要使用名词表示资源，而不是使用动词描述操作。

例如：

```text
/api/users           # 用户资源集合
/api/users/1001      # ID为1001的用户
/api/sessions        # 会话资源集合
/api/sessions/abc123 # ID为abc123的会话
```

**（2）使用 HTTP 方法定义操作**

以会话管理为例：

| HTTP 方法 | 请求 URL              | 功能             |
| --------- | --------------------- | ---------------- |
| GET       | `/api/sessions`     | 获取会话列表     |
| GET       | `/api/sessions/123` | 获取指定会话     |
| POST      | `/api/sessions`     | 创建新会话       |
| PUT       | `/api/sessions/123` | 整体更新指定会话 |
| PATCH     | `/api/sessions/123` | 部分更新指定会话 |
| DELETE    | `/api/sessions/123` | 删除指定会话     |

同一个资源可以通过不同 HTTP 方法执行不同操作，从而避免为每项操作设计独立的动词式 URL。

**（3）设计请求参数**

例如，创建一个新会话：

```http
POST /api/sessions HTTP/1.1
Content-Type: application/json

{
    "model": "qwen3",
    "title": "新的对话"
}
```

请求主要包含：

- **请求方法**：`POST`，表示创建资源。
- **请求路径**：`/api/sessions`，表示会话资源集合。
- **请求头**：`Content-Type` 声明请求体的数据格式。
- **请求体**：使用 JSON 提供模型名称、会话标题等参数。

**（4）设计统一响应格式**

接口可以统一使用 `success`、`message`、`data` 三个字段。

例如，创建会话成功后返回：

```json
{
    "success": true,
    "message": "Session created successfully",
    "data": {
        "session_id": "abc123",
        "model": "qwen3",
        "title": "新的对话"
    }
}
```

同时使用标准 HTTP 状态码表示处理结果：

| 状态码                    | 含义             |
| ------------------------- | ---------------- |
| 200 OK                    | 请求成功         |
| 201 Created               | 资源创建成功     |
| 400 Bad Request           | 请求参数错误     |
| 401 Unauthorized          | 缺少有效身份认证 |
| 404 Not Found             | 请求资源不存在   |
| 500 Internal Server Error | 服务器内部错误   |

### 3. 项目扩展方向

当前主要提供 C++ 接口供应用程序调用，后续可以在 SDK 基础上封装 RESTful API，构建独立的 HTTP 服务。

服务端可以使用 `cpp-httplib` 注册请求路由，根据 HTTP 方法和 URL 将请求分发到对应处理函数，由处理函数调用 ChatSDK 完成模型交互、会话管理等操作，并将结果封装为 JSON 或流式响应返回客户端。

通过这一扩展，可以让浏览器、移动端以及其他编程语言编写的客户端，通过统一的 HTTP 接口使用 ChatSDK 的功能，降低 SDK 的接入成本，提高系统的可扩展性与跨平台使用能力。

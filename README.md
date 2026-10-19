# ModelBridge — C++ 多模型统一接入框架

## 1. 项目概述

### 1.1 项目介绍

ModelBridge — C++ 多模型统一接入框架 是一个基于 C++ 开发的大语言模型接入与聊天系统。

项目旨在通过统一的 ChatSDK 接口，屏蔽不同大模型服务之间的调用差异，实现云端大模型与本地大模型的统一管理，并在此基础上开发支持多模型、多轮会话、流式响应和历史记录管理的 AI 聊天应用。

**核心目标：**

- 掌握基于 HTTP/HTTPS 协议的大模型 API 调用。
- 使用 C++ 接入 DeepSeek、OpenAI、Gemini 等云端大模型。
- 通过 Ollama 接入本地部署的大语言模型。
- 封装统一的大模型访问接口，降低业务代码与具体模型服务的耦合。
- 实现多轮对话、会话管理和消息持久化。
- 支持同步响应与流式响应。
- 在 ChatSDK 基础上构建完整的 AI 聊天应用。

### 1.2 项目主要功能

| 功能模块     | 功能描述                                 |
| ------------ | ---------------------------------------- |
| 模型管理     | 获取支持的模型、初始化和管理不同模型     |
| 云端模型接入 | 调用 DeepSeek、OpenAI、Gemini 等模型 API |
| 本地模型接入 | 通过 Ollama 调用本地部署的模型           |
| 会话创建     | 为指定模型创建独立会话                   |
| 会话管理     | 获取会话列表、查询会话信息、删除会话     |
| 多轮对话     | 根据历史消息维护对话上下文               |
| 普通响应     | 等待模型生成完成后，一次性返回完整结果   |
| 流式响应     | 模型生成部分内容后，逐步返回给调用方     |
| 数据持久化   | 使用 SQLite 保存会话及历史消息           |
| 日志管理     | 通过 spdlog 记录运行信息与错误           |

---

## 2. 项目整体架构

### 2.1 分层设计

项目采用分层设计，主要由应用层、ChatSDK 核心层、数据存储层和模型服务层构成。

```mermaid
flowchart TD
    A["Web 前端 / 客户端"]
    B["ChatServer（HTTP 服务）"]
    C["ChatSDK（统一大模型接入层）"]

    D["LLMManager"]
    E["SessionManager"]
    F["DataManager"]

    G["DeepSeekProvider"]
    H["ChatGPTProvider"]
    I["GeminiProvider"]
    J["OllamaProvider"]

    K["DeepSeek API"]
    L["OpenAI API"]
    M["Gemini API"]
    N["本地 Ollama 服务"]

    O[("SQLite 数据库")]

    A <-->|HTTP| B
    B --> C

    C --> D
    C --> E
    C --> F

    D --> G
    D --> H
    D --> I
    D --> J

    G <-->|HTTPS| K
    H <-->|HTTPS| L
    I <-->|HTTPS| M
    J <-->|HTTP| N

    E --> F
    F <--> O
```

### 2.2 核心模块职责

#### ChatServer：应用服务层

负责向客户端提供 HTTP 接口。

主要职责：

- 接收客户端请求。
- 解析请求参数。
- 调用 ChatSDK。
- 将处理结果返回客户端。
- 将流式模型输出转发给客户端。

#### ChatSDK：统一接入层

ChatSDK 是项目的核心模块。

它主要负责：

- 模型初始化与管理。
- 创建和管理会话。
- 调用指定模型。
- 管理历史聊天消息。
- 提供普通和流式消息接口。
- 协调会话数据的持久化。

#### LLMManager：模型管理器

负责管理不同的大模型 Provider。

不同模型的访问逻辑分别由对应的 Provider 实现，LLMManager 对它们进行统一管理。

这样可以避免业务层针对每个模型分别编写调用代码。

#### Provider：模型访问实现

Provider 负责具体模型的 API 调用。

例如：

- `DeepSeekProvider`：对接 DeepSeek。
- `ChatGPTProvider`：对接 OpenAI。
- `GeminiProvider`：对接 Gemini。
- `OllamaProvider`：对接本地 Ollama。

不同 Provider 对应不同的请求地址、认证方式、请求体及响应解析逻辑。

这里使用的是功能上的名称；实际源码中的类名应以当前 SDK 为准，例如你安装的版本中存在 `OllamaLLMProvider.h`。

#### SessionManager：会话管理器

负责管理聊天会话及其消息记录。

主要职责：

- 创建会话。
- 根据会话 ID 查找会话。
- 获取会话列表。
- 管理消息上下文。
- 删除会话。

#### DataManager：数据管理器

负责将会话及历史消息持久化到 SQLite 数据库。

通过持久化存储，可以避免程序退出后所有聊天记录都只保留在内存中。

## 3. 核心技术栈

### 3.1 第三方库

| 技术        | 功能                 | 在项目中的作用               |
| ----------- | -------------------- | ---------------------------- |
| C++17       | 开发语言标准         | 实现 ChatSDK 和业务服务      |
| CMake       | 项目构建             | 管理编译与链接               |
| cpp-httplib | HTTP 客户端/服务器库 | 发送模型请求、提供 HTTP 接口 |
| JsonCpp     | JSON 处理库          | JSON 序列化和反序列化        |
| spdlog      | 日志库               | 日志记录与问题排查           |
| fmt         | 字符串格式化         | 格式化输出                   |
| SQLite      | 嵌入式数据库         | 存储会话及历史消息           |
| gflags      | 命令行参数解析       | 管理启动参数和配置           |
| OpenSSL     | TLS/SSL              | 支持 HTTPS 通信              |
| GoogleTest  | C++ 测试框架         | 编写单元测试                 |
| curl        | HTTP 测试工具        | 调试模型 API                 |
| Ollama      | 本地模型运行环境     | 加载模型并提供推理接口       |
| Postman     | API 调试工具         | 测试 HTTP 请求和响应         |

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
6. 解析 JSON 并提取模型生成内容。

### 4.2 常见消息角色

| role      | 含义                     |
| --------- | ------------------------ |
| system    | 设置模型的角色和行为要求 |
| user      | 用户输入                 |
| assistant | 模型之前生成的回复       |
| tool      | 工具调用执行结果         |

其中，`system`、`user` 和 `assistant` 是普通聊天中最常用的三类角色。

例如：

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

---

## 5. ChatSDK 核心接口

### 5.1 ChatSDK 的职责

### 5.2 ChatSDK 主要 API

### 5.3 核心调用流程

### 5.4 普通响应与流式响应

#### 普通响应

#### 流式响应

## 6. 开发环境与项目构建

### 6.1 开发环境

课程采用 Linux 环境和 CMake 构建系统，推荐使用支持 Remote SSH 的 IDE 进行开发。

常用开发工具：

- VS Code / Trae 等 C++ IDE。
- clangd：代码补全、错误检查和代码导航。
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
    libgflags-dev \
    libspdlog-dev \
    libfmt-dev \
    libjsoncpp-dev \
    libcurl4-openssl-dev \
    libgtest-dev \
    libssl-dev \
    libsqlite3-dev
```

其中：

- `libfmt-dev` 提供 fmt 开发头文件及库。
- `libsqlite3-dev` 提供 SQLite 开发文件。
- `libssl-dev` 提供 OpenSSL 开发文件。
- `build-essential` 提供常用 C/C++ 编译工具。

### 6.3 cpp-httplib 安装

cpp-httplib 是轻量级 C++ HTTP 库，常用的单头文件版本可以直接通过 `httplib.h` 使用。

克隆源码：

```bash
git clone https://github.com/yhirose/cpp-httplib.git
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

## 8. LLMProvider 封装的三种聊天协议

本节对应 `SDK/include/LLMProvider.hpp` 和 `SDK/src/LLMProvider.cpp` 的当前实现。
三种协议均通过 HTTP `POST` 发送 JSON，但请求路径、认证头、消息结构和流式格式不同。
`baseUrl` 是服务根地址，`requestUrl` 是最终请求地址：

```text
requestUrl = baseUrl（去掉末尾 /）+ Provider 生成的路径
```

### 8.1 baseUrl 与请求 URL

| 协议 / 服务                | baseUrl 示例                                          | 普通请求路径                        | 流式请求路径                                      |
| -------------------------- | ----------------------------------------------------- | ----------------------------------- | ------------------------------------------------- |
| OpenAI 兼容 / OpenAI       | `https://api.openai.com/v1`                         | `/chat/completions`               | `/chat/completions`                             |
| OpenAI 兼容 / DeepSeek     | `https://api.deepseek.com`                          | `/chat/completions`               | `/chat/completions`                             |
| OpenAI 兼容 / 百炼北京区域 | `https://dashscope.aliyuncs.com/compatible-mode/v1` | `/chat/completions`               | `/chat/completions`                             |
| Gemini 原生                | `https://generativelanguage.googleapis.com/v1beta`  | `/models/{model}:generateContent` | `/models/{model}:streamGenerateContent?alt=sse` |
| Ollama 原生                | `http://127.0.0.1:11434`                            | `/api/chat`                       | `/api/chat`                                     |

例如，OpenAI 的完整地址是 `https://api.openai.com/v1/chat/completions`，
DeepSeek 是 `https://api.deepseek.com/chat/completions`。
百炼不同区域有不同地址，需要与 API Key 所属区域匹配。Ollama 的 `baseUrl` 若已包含末尾 `/api`，当前实现只追加 `/chat`，避免重复拼接。

依据：[OpenAI Chat Completions](https://developers.openai.com/api/reference/resources/chat)、[DeepSeek 接入说明](https://api-docs.deepseek.com/)、[百炼 OpenAI 兼容接口](https://help.aliyun.com/zh/model-studio/compatibility-of-openai-with-dashscope)。

### 8.2 OpenAI 兼容协议

对应 `OpenAICompatibleProvider` 和 `ApiProtocol::OpenAICompatible`。OpenAI、DeepSeek、百炼以及兼容此协议的中转服务可以共用该实现。

**请求头**

| 请求头            | 值                    | 说明                       |
| ----------------- | --------------------- | -------------------------- |
| `Content-Type`  | `application/json`  | 请求体为 JSON              |
| `Authorization` | `Bearer <API_KEY>`  | 当前 Provider 要求提供密钥 |
| `Accept`        | `application/json`  | 普通请求                   |
| `Accept`        | `text/event-stream` | 流式请求                   |

**请求体参数**

| 参数                        | 是否必填          | 当前封装的含义                                                  |
| --------------------------- | ----------------- | --------------------------------------------------------------- |
| `model`                   | 是                | `ModelInfo::modelName`，请求使用的模型 ID                     |
| `messages`                | 是                | 按顺序保存的完整上下文，不能为空                                |
| `messages[].role`         | 是                | `system`、`user`、`assistant`；工具结果使用 `tool`      |
| `messages[].content`      | 是                | 文本内容                                                        |
| `messages[].tool_call_id` | Tool 消息需要     | 对应 `Message::toolCallId`，关联原工具调用                    |
| `stream`                  | Provider 总是发送 | `sendMessage` 为 `false`，`sendMessageStream` 为 `true` |
| `temperature`             | 否                | 配置了 `temperature` 才发送，包括显式的 `0`                 |
| `max_completion_tokens`   | 否                | `provider == ProviderType::OpenAI` 时使用的输出上限字段       |
| `max_tokens`              | 否                | 当前封装对其他兼容平台使用的输出上限字段                        |

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

| 响应参数                       | 含义                                                        | 当前 SDK 的处理                              |
| ------------------------------ | ----------------------------------------------------------- | -------------------------------------------- |
| `choices[0].message.content` | 第一个候选回答的文本                                        | 作为返回字符串                               |
| `choices[].finish_reason`    | 如 `stop`、`length`、`tool_calls`、`content_filter` | 长度截断保留已有文本；工具调用或内容拦截报错 |
| `usage`                      | 输入、输出及总 Token 统计                                   | 当前文本接口不返回统计                       |
| `id`、`model`              | 服务端响应 ID、实际模型                                     | 不作为回复文本返回                           |

**流式响应：SSE**

请求仍发往 `/chat/completions`，只需将 `stream` 设为 `true`。响应示意：

```text
data: {"choices":[{"delta":{"role":"assistant","content":"Reactor"},"finish_reason":null}]}

data: {"choices":[{"delta":{"content":" 是一种事件驱动模型。"},"finish_reason":null}]}

data: {"choices":[{"delta":{},"finish_reason":"stop"}]}

data: [DONE]
```

可见文本位于 `choices[0].delta.content`，按增量拼接。角色事件或用量事件可以没有文本，不能因此判断响应结束。当前封装以 `[DONE]` 判断 OpenAI 兼容流已完整结束。

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

| 参数                                 | 是否必填     | 当前封装的含义                                   |
| ------------------------------------ | ------------ | ------------------------------------------------ |
| `contents`                         | 是           | 用户及模型的对话历史，至少包含一条非 System 消息 |
| `contents[].role`                  | 当前封装发送 | User 映射为 `user`，Assistant 映射为 `model` |
| `contents[].parts[].text`          | 文本请求需要 | 一条消息的文本内容                               |
| `systemInstruction.parts[].text`   | 否           | System 消息放在这里，不放入 `contents`         |
| `generationConfig.temperature`     | 否           | 采样温度                                         |
| `generationConfig.maxOutputTokens` | 否           | 输出 Token 上限                                  |

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

| 响应参数                               | 含义                     | 当前 SDK 的处理                                       |
| -------------------------------------- | ------------------------ | ----------------------------------------------------- |
| `candidates[0].content.parts[].text` | 第一个候选回答的文本片段 | 按顺序拼接，不包含 `thought: true` 的片段           |
| `candidates[0].finishReason`         | 生成结束原因             | 接受 `STOP`、`MAX_TOKENS`；其他非空原因按失败处理 |
| `promptFeedback.blockReason`         | 输入被拦截的原因         | 抛出异常                                              |
| `usageMetadata`                      | Token 使用情况           | 当前文本接口不返回统计                                |

**流式响应：SSE**

使用 `:streamGenerateContent?alt=sse`，每个 SSE `data:` 事件仍是一个原生 JSON 对象，
文本位置仍为 `candidates[0].content.parts[].text`：

```text
data: {"candidates":[{"content":{"role":"model","parts":[{"text":"Reactor"}]}}]}

data: {"candidates":[{"content":{"role":"model","parts":[{"text":" 是一种事件驱动模型。"}]},"finishReason":"STOP"}]}
```

Gemini 原生流不依赖 OpenAI 的 `[DONE]` 标记。当前实现要求出现可接受的 `finishReason`，
并且 HTTP 传输正常完成，才通知调用方结束。

依据：[Gemini API](https://ai.google.dev/api/generate-content)、[文本生成及请求头示例](https://ai.google.dev/gemini-api/docs/text-generation)。

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

| 参数                                        | 是否必填          | 当前封装的含义                                                  |
| ------------------------------------------- | ----------------- | --------------------------------------------------------------- |
| `model`                                   | 是                | 已安装的完整模型名，包含实际版本或量化标签                      |
| `messages`                                | 是                | 当前封装支持 System、User、Assistant 文本历史                   |
| `messages[].role`、`messages[].content` | 是                | 消息角色与文本                                                  |
| `stream`                                  | Provider 总是发送 | 普通为 `false`，流式为 `true`；原生 API 的默认值是 `true` |
| `options.temperature`                     | 否                | 采样温度，放在 `options` 中                                   |
| `options.num_predict`                     | 否                | 由 `maxOutputTokens` 映射出的输出上限                         |

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

| 响应参数                              | 含义                       | 当前 SDK 的处理                             |
| ------------------------------------- | -------------------------- | ------------------------------------------- |
| `message.content`                   | 可见回复文本               | 作为返回字符串                              |
| `message.thinking`                  | 模型的思考内容，可能缺省   | 不混入可见回复                              |
| `done`                              | 本次生成是否完成           | 普通响应必须为 `true`；流式也用它判断结束 |
| `done_reason`                       | 如自然结束或达到长度限制   | 当前返回接口不单独暴露                      |
| `prompt_eval_count`、`eval_count` | 输入及输出 Token 数量      | 当前返回接口不单独暴露                      |
| `total_duration` 等耗时字段         | 原生响应的计时，单位为纳秒 | 当前返回接口不单独暴露                      |

**流式响应：NDJSON**

仍调用 `/api/chat`，将 `stream` 设为 `true`，每行是一个独立 JSON 对象：

```json
{"message":{"role":"assistant","content":"Reactor"},"done":false}
{"message":{"role":"assistant","content":" 是一种事件驱动模型。"},"done":false}
{"message":{"role":"assistant","content":""},"done":true}
```

NDJSON 没有 SSE 的 `data:` 前缀，也没有 `[DONE]`。最后一条记录可以同时含有新增文本、
结束状态和统计信息，应先处理文本，再处理结束状态。

依据：[Ollama Chat](https://docs.ollama.com/api/chat)、[Ollama 流式协议](https://docs.ollama.com/api/streaming)。

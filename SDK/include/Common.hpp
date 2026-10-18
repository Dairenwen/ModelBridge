#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @file Common.hpp
 * @brief 基础文本聊天所需的公共数据类型，使用 C++17 标准库。
 *
 * Provider 负责不同协议的地址拼接、认证及字段转换。
 * 结构体只保存数据；ID 生成、参数校验和持久化由对应管理器负责。
 */
namespace modelBridge
{

    /// 模型服务平台
    enum class ProviderType
    {
        Custom,  ///< 自定义或尚未指定的平台。
        OpenAI,  ///< OpenAI 平台。
        Gemini,  ///< Google Gemini 平台。
        Ollama,  ///< Ollama 服务。
        Bailian, ///< 阿里云百炼；使用 OpenAI 兼容接口时配置 OpenAICompatible。
        DeepSeek ///< DeepSeek 平台；聊天接口使用 OpenAICompatible。
    };

    /// 请求协议；云端或本地部署由 baseUrl 决定，不再单独配置部署类型。
    enum class ApiProtocol
    {
        OpenAICompatible, ///< OpenAI 风格的聊天协议，可用于兼容该协议的服务。
        Gemini,           ///< Gemini 原生聊天协议。
        Ollama            ///< Ollama 原生 /api/chat 协议。
    };

    /// 聊天消息角色；工具调用请求仍由 Assistant 发出，工具结果使用 Tool。
    enum class MessageRole
    {
        System,    ///< 系统指令。
        User,      ///< 用户输入。
        Assistant, ///< 模型回复或工具调用请求。
        Tool       ///< 工具执行后返回的结果。
    };

    /**
     * @brief 获取当前时间，用于记录消息和会话的创建时间。
     * @return 自 Unix 纪元起的毫秒数。
     */
    inline std::int64_t currentTimeMillis()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    /// 一条文本消息；会话中的排列顺序即对话顺序。
    struct Message
    {
        MessageRole role;                             ///< 消息角色。
        std::string content;                          ///< UTF-8 文本内容。
        std::int64_t createdAt = currentTimeMillis(); ///< 创建时间，Unix 毫秒。
        std::string toolCallId;                       ///< Tool 消息关联的工具调用 ID，其他角色留空。
    };

    /// 模型的基本信息，不包含连接密钥。
    struct ModelInfo
    {
        std::string modelName;   ///< API 使用的模型标识，本地模型需包含实际标签。
        std::string displayName; ///< 展示名称；为空时使用 modelName。
        ApiProtocol protocol;    ///< 使用的聊天协议。
        ProviderType provider;   ///< 服务平台；百炼和 DeepSeek 可共用兼容协议。
    };

    /**
     * @brief 一个模型连接及其基础调用参数。
     * Provider 优先使用非空 apiKey，否则从 apiKeyEnv 指定的环境变量读取。
     * 需要认证的服务未获得有效密钥时应报错；无需认证的本地 Ollama 可将两者留空。
     */
    struct ModelConfig
    {
        std::string configId;               ///< 用户范围内唯一的连接 ID，用于区分同名模型的不同连接。
        ModelInfo model;                    ///< 模型名称及协议。
        std::string baseUrl;                ///< 服务根地址，可包含版本前缀；具体聊天路径由 Provider 拼接。
        std::string apiKeyEnv;              ///< 密钥环境变量名，apiKey 为空时由 Provider 读取。
        std::optional<double> temperature;  ///< 采样温度；未设置时使用服务端默认值。
        std::optional<int> maxOutputTokens; ///< 输出 Token 上限，设置时须大于零。
        int timeoutSeconds = 120;           ///< 请求超时秒数，须大于零。
        bool stream = false;                ///< 是否默认使用流式回复。
        std::string apiKey;                 ///< 运行时直接传入的密钥
    };

    /// 一个聊天会话及其完整历史。
    struct Session
    {
        std::string sessionId;                        ///< 会话唯一 ID。
        std::string userId;                           ///< 所属用户 ID。
        std::string modelConfigId;                    ///< 使用的 ModelConfig::configId。
        std::string title;                            ///< 会话标题。
        std::vector<Message> messages;                ///< 按对话顺序保存的历史，系统提示也作为消息保存。
        std::int64_t createdAt = currentTimeMillis(); ///< 创建时间，Unix 毫秒。
    };

    /// 用户自己的基础配置。
    struct UserConfig
    {
        std::string userId;                    ///< 用户唯一 ID。
        std::vector<ModelConfig> modelConfigs; ///< 用户配置的模型连接。
        std::string defaultModelConfigId;      ///< 默认连接 ID，须对应 modelConfigs 中的一项。
        std::string defaultSystemPrompt;       ///< 新会话的系统提示，由管理器加入首条 System 消息。
        std::string databasePath;              ///< SQLite 数据库文件路径。
    };

    /**
     * @brief 接收流式回复；所有片段按顺序拼接后得到完整文本。
     * @param delta 本次新增文本，可为空；引用仅在回调期间有效。
     * @param done 是否为本次回复的最后一次回调；先处理 delta，再处理结束标记。
     * @return 无。
     */
    using StreamCallback = std::function<void(const std::string &delta, bool done)>;

} // namespace modelBridge

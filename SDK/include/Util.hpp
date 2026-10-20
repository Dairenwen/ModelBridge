#pragma once
#include "Common.hpp"
#include <json/json.h>
#include <memory>
#include <stdexcept>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace modelBridge
{
    /// 解析一个完整 JSON 对象；错误信息不带响应正文。
    inline Json::Value parseJson(const std::string &text)
    {
        Json::CharReaderBuilder builder;
        builder["failIfExtra"] = true;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value value;
        std::string error;
        if (!reader->parse(text.data(), text.data() + text.size(), &value, &error) || !value.isObject())
            throw std::runtime_error("Invalid JSON object in model response");
        if (value.isMember("error"))
            throw std::runtime_error("Model service returned an error");
        return value;
    }

    /// null 表示当前事件没有文本；其他非字符串值视为协议错误。
    inline std::string readText(const Json::Value &value)
    {
        if (value.isNull())
            return {};
        if (!value.isString())
            throw std::runtime_error("Invalid response text");
        return value.asString();
    }

    /// OpenAI 和 Ollama 使用的消息角色名称。
    inline const char *roleName(MessageRole role)
    {
        switch (role)
        {
        case MessageRole::System:
            return "system";
        case MessageRole::User:
            return "user";
        case MessageRole::Assistant:
            return "assistant";
        case MessageRole::Tool:
            return "tool";
        }
        throw std::invalid_argument("Unknown message role");
    }

    /// 转换共用的 role/content 结构；仅兼容协议允许携带工具结果 ID。
    inline Json::Value makeChatMessages(const std::vector<Message> &messages, ApiProtocol protocol)
    {
        Json::Value result(Json::arrayValue);
        for (const auto &message : messages)
        {
            Json::Value item;
            item["role"] = roleName(message.role);
            item["content"] = message.content;
            if (message.role == MessageRole::Tool)
            {
                if (protocol == ApiProtocol::OpenAICompatible)
                {
                    if (message.toolCallId.empty())
                        throw std::invalid_argument("OpenAI-compatible tool results require tool_call_id");
                    item["tool_call_id"] = message.toolCallId;
                }
                else if (protocol == ApiProtocol::Ollama)
                {
                    if (!message.toolName.empty())
                        item["tool_name"] = message.toolName;

                    if (!message.toolCallId.empty())
                        item["tool_call_id"] = message.toolCallId;
                }
                else
                {
                    throw std::invalid_argument("Gemini tool results must use functionResponse");
                }
            }
            result.append(item);
        }
        return result;
    }

    /// 仅检查 SDK 无法表示或会产生歧义的组合，模型参数范围交给服务端校验。
    inline void validateReasoning(const ModelConfig &config)
    {
        if (!config.thinkingEnabled.has_value())
            return;
        const auto platform = config.model.provider;
        switch (config.model.protocol)
        {
        case ApiProtocol::Gemini:
            throw std::invalid_argument("Gemini uses reasoningEffort, not thinkingEnabled");
        case ApiProtocol::Ollama:
            if (config.reasoningEffort)
                throw std::invalid_argument("Ollama accepts either reasoningEffort or thinkingEnabled");
            return;
        case ApiProtocol::OpenAICompatible:
            if (platform != ProviderType::DeepSeek && platform != ProviderType::Bailian)
                throw std::invalid_argument("thinkingEnabled requires DeepSeek or Bailian");
            if (!config.reasoningEffort)
                return;
            const bool effortEnabled = *config.reasoningEffort != "none";
            if ((platform == ProviderType::DeepSeek && *config.thinkingEnabled != effortEnabled) ||
                (platform == ProviderType::Bailian && !*config.thinkingEnabled && effortEnabled))
                throw std::invalid_argument("thinkingEnabled conflicts with reasoningEffort");
        }
    }

    /**
     * 流式响应分帧缓冲，不依赖具体 HTTP 库。SSE 用空行分隔事件，Ollama 的 NDJSON 每行就是一个事件。
     */
    struct StreamDecoder
    {
        std::string pending, event;
        bool ndjson = false;
        std::function<void(const std::string &)> onEvent;

        /// 拼接网络分片
        void append(const char *data, size_t length)
        {
            pending.append(data, length);
            auto end = pending.find('\n');
            while (end != std::string::npos)
            {
                auto line = pending.substr(0, end);
                pending.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (ndjson)
                {
                    if (!line.empty())
                        onEvent(line);
                }
                else if (line.empty() && !event.empty())
                {
                    event.pop_back(); // 去掉最后一个 data 行追加的换行
                    if (!event.empty())
                        onEvent(event);
                    event.clear();
                }
                else if (line == "data" || line.compare(0, 5, "data:") == 0)
                {
                    auto value = line.substr(line == "data" ? 4 : 5);
                    if (!value.empty() && value.front() == ' ')
                        value.erase(0, 1);
                    event += value + '\n';
                }
                end = pending.find('\n');
            }
        }

        /// 统一处理末尾没有换行的数据，完整事件仍交给 onEvent。
        void finish() { append("\n\n", 2); }
    };

    /// 共用的异步终端日志，默认输出 WARN 及以上级别。
    class Logger
    {
    public:
        /**
         * @brief 获取 SDK 共用的异步日志对象，首次调用时自动创建。
         * @return 日志对象的引用，可用于调整 spdlog 的其他设置。
         */
        static spdlog::logger &getLogger()
        {
            // 函数内静态变量保证跨源文件共用，且首次初始化是线程安全的。
            static auto logger = []
            {
                // 先初始化输出端的内部锁，再创建线程池，确保退出时锁仍可用于输出。
                auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                // 独立队列容纳 8192 条消息，单个后台线程按入队顺序输出。线程池先于日志对象创建，正常退出时等待队列中的日志处理完成。
                static auto threadPool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
                // 队列满时等待空位，不丢弃日志；实际 stdout 输出由后台线程执行。
                auto instance = std::make_shared<spdlog::async_logger>("modelBridge", sink, threadPool, spdlog::async_overflow_policy::block);
                instance->set_level(spdlog::level::warn);
                instance->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] [%s:%#] %v");
                instance->flush_on(spdlog::level::warn); // 刷新也在后台线程执行
                return instance;
            }();
            return *logger;
        }

        /**
         * @brief 设置最低输出级别，例如 spdlog::level::debug 或 spdlog::level::off。
         * @param level 最低输出级别，低于它的日志不输出。
         * @return 无。
         */
        static void setLevel(spdlog::level::level_enum level)
        {
            getLogger().set_level(level);
        }
    };

} // namespace modelBridge

/// 输出 TRACE 级别日志，默认不打印。
#define MB_LOG_TRACE(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::trace, __VA_ARGS__)
/// 输出 DEBUG 级别日志，默认不打印。
#define MB_LOG_DEBUG(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::debug, __VA_ARGS__)
/// 输出 INFO 级别日志，默认不打印。
#define MB_LOG_INFO(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::info, __VA_ARGS__)
/// 输出 WARN 级别日志。
#define MB_LOG_WARN(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::warn, __VA_ARGS__)
/// 输出 ERROR 级别日志。
#define MB_LOG_ERROR(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::err, __VA_ARGS__)
/// 输出 CRITICAL 级别日志。
#define MB_LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::critical, __VA_ARGS__)

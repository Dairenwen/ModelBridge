#include "LLMProvider.hpp"
#include <curl/curl.h>
#include <json/json.h>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace
{
    // curl 全局状态先于请求创建，退出时统一释放，不在每次请求间反复初始化。
    struct CurlRuntime
    {
        CurlRuntime()
        {
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                throw std::runtime_error("Unable to initialize libcurl");
        }
        ~CurlRuntime() { curl_global_cleanup(); }
    };

    /// 解析单个 JSON 文档，严格拒绝尾部垃圾，错误中不回显密钥或响应正文。
    Json::Value parseJson(const std::string &text)
    {
        Json::CharReaderBuilder builder;
        builder["failIfExtra"] = true;
        builder["allowComments"] = false;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        Json::Value value;
        std::string errors;
        if (!reader->parse(text.data(), text.data() + text.size(), &value, &errors) || !value.isObject())
            throw std::runtime_error("Invalid JSON object in model response");
        if (value.isMember("error"))
            throw std::runtime_error("Model service returned an error");
        return value;
    }

    /// 公共消息角色映射；不把非法枚举静默视为用户消息。
    std::string roleName(modelBridge::MessageRole role)
    {
        using modelBridge::MessageRole;
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

    /// JSON 中的文本字段必须为字符串；缺省和 null 可表示无文本的增量。
    std::string optionalText(const Json::Value &value)
    {
        if (value.isNull())
            return {};
        if (!value.isString())
            throw std::runtime_error("Expected a text field in model response");
        return value.asString();
    }

    /// 请求状态只在一次调用内使用；C 回调捕获异常，避免异常跨越 C ABI。
    struct TransferState
    {
        long status = 0;                                  ///< 最近一组 HTTP 响应头的状态码。
        std::string body;                                 ///< 普通响应或 HTTP 错误响应。
        std::function<void(const std::string &)> receive; ///< 流式数据处理器。
        std::exception_ptr exception;                     ///< 回调内抛出的解析或用户回调异常。
    };

    size_t receiveHeader(char *data, size_t size, size_t count, void *userData)
    {
        auto &state = *static_cast<TransferState *>(userData);
        const auto length = size * count;
        try
        {
            const std::string line(data, length);
            if (line.compare(0, 5, "HTTP/") == 0)
            {
                const auto space = line.find(' ');
                if (space == std::string::npos)
                    throw std::runtime_error("Invalid HTTP status line");
                state.status = std::stol(line.substr(space + 1));
            }
            return length;
        }
        catch (...)
        {
            state.exception = std::current_exception();
            return 0;
        }
    }

    size_t receiveBody(char *data, size_t size, size_t count, void *userData)
    {
        auto &state = *static_cast<TransferState *>(userData);
        const auto length = size * count;
        try
        {
            if (state.receive && state.status >= 200 && state.status < 300)
                state.receive(std::string(data, length));
            else
                state.body.append(data, length);
            return length;
        }
        catch (...)
        {
            state.exception = std::current_exception();
            return 0;
        }
    }

    /**
     * 按协议解帧，而不是把 HTTP 数据块当成完整 JSON。
     * SSE 以空行结束事件，可有多行 data；NDJSON 每行独立，允许最后一行无换行。
     */
    class StreamDecoder
    {
    public:
        StreamDecoder(bool ndjson, std::function<void(const std::string &)> consume)
            : ndjson(ndjson), consume(std::move(consume)) {}

        void feed(const std::string &chunk)
        {
            buffer += chunk;
            std::size_t end;
            while ((end = buffer.find('\n')) != std::string::npos)
            {
                auto line = buffer.substr(0, end);
                buffer.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                processLine(line);
            }
        }

        void finish()
        {
            if (!buffer.empty())
            {
                if (buffer.back() == '\r')
                    buffer.pop_back();
                processLine(buffer);
                buffer.clear();
            }
            dispatchEvent();
        }

    private:
        bool ndjson;                                      ///< true 为 Ollama，false 为 SSE。
        std::function<void(const std::string &)> consume; ///< 接收完整帧。
        std::string buffer;                               ///< 尚未接收完整的一行。
        std::string eventData;                            ///< SSE 事件中拼接的 data 字段。

        void dispatchEvent()
        {
            if (eventData.empty())
                return;
            eventData.pop_back(); // 去掉按 SSE 规则追加的最后一个换行。
            if (!eventData.empty())
                consume(eventData);
            eventData.clear();
        }

        void processLine(const std::string &line)
        {
            if (ndjson)
            {
                if (!line.empty())
                    consume(line);
            }
            else if (line.empty())
                dispatchEvent();
            else if (line == "data" || line.compare(0, 5, "data:") == 0)
            {
                auto value = line == "data" ? std::string{} : line.substr(5);
                if (!value.empty() && value.front() == ' ')
                    value.erase(0, 1);
                eventData += value + '\n';
            }
            // SSE 注释、event、id、retry 均不属于模型 JSON 内容。
        }
    };
}

namespace modelBridge
{
    bool LLMProvider::initialize(const ModelConfig &modelConfig)
    {
        configurationValid = false;
        lastError.clear();
        try
        {
            config = modelConfig;
            if (config.model.protocol != getProtocol())
                throw std::invalid_argument("Provider protocol does not match model configuration");
            if (config.model.modelName.empty())
                throw std::invalid_argument("Model name is required");
            // baseUrl 必须是服务根地址；不接受带密钥的 URL、查询串或片段。
            const auto scheme = config.baseUrl.find("://");
            if (scheme == std::string::npos ||
                (config.baseUrl.substr(0, scheme) != "http" && config.baseUrl.substr(0, scheme) != "https") ||
                config.baseUrl.find_first_of("?#@\r\n\t ") != std::string::npos)
                throw std::invalid_argument("baseUrl must be an HTTP(S) service root without credentials or query");
            while (!config.baseUrl.empty() && config.baseUrl.back() == '/')
                config.baseUrl.pop_back();
            if (config.baseUrl.size() <= scheme + 3)
                throw std::invalid_argument("baseUrl host is required");
            if (config.timeoutSeconds <= 0 || (config.maxOutputTokens && *config.maxOutputTokens <= 0))
                throw std::invalid_argument("Timeout and output token limit must be positive");
            if (config.temperature && (!std::isfinite(*config.temperature) || *config.temperature < 0))
                throw std::invalid_argument("Temperature must be finite and non-negative");
            static CurlRuntime runtime;
            if (config.apiKey.empty() && !config.apiKeyEnv.empty())
            {
                if (const auto *value = std::getenv(config.apiKeyEnv.c_str()))
                    config.apiKey = value;
            }
            if (config.apiKey.find_first_of("\r\n") != std::string::npos)
                throw std::invalid_argument("API key contains a newline");
            configurationValid = true;
            if (!isInitialized())
                throw std::invalid_argument("API key is required for this protocol");
            return true;
        }
        catch (const std::exception &error)
        {
            lastError = error.what();
            return false;
        }
    }

    bool LLMProvider::isInitialized() const
    {
        return configurationValid && (getProtocol() == ApiProtocol::Ollama || !config.apiKey.empty());
    }

    const std::string &LLMProvider::getModelName() const { return config.model.modelName; }
    const std::string &LLMProvider::getModelDescription() const { return config.model.description; }
    const ModelInfo &LLMProvider::getModelInfo() const { return config.model; }
    const std::string &LLMProvider::getLastError() const { return lastError; }
    const std::string &LLMProvider::getApiKey() const { return config.apiKey; }

    void LLMProvider::setApiKey(const std::string &apiKey)
    {
        if (apiKey.find_first_of("\r\n") != std::string::npos)
            throw std::invalid_argument("API key contains a newline");
        config.apiKey = apiKey;
        if (isInitialized())
            lastError.clear();
    }

    std::string LLMProvider::sendMessage(const std::vector<Message> &messages)
    {
        return performRequest(messages, false, {});
    }

    std::string LLMProvider::sendMessageStream(const std::vector<Message> &messages,
                                               const StreamCallback &callback)
    {
        if (!callback)
            throw std::invalid_argument("Stream callback is required");
        return performRequest(messages, true, callback);
    }

    std::string LLMProvider::performRequest(const std::vector<Message> &messages, bool stream,
                                            const StreamCallback &callback)
    {
        if (!isInitialized())
            throw std::logic_error("Provider is not initialized");
        if (messages.empty())
            throw std::invalid_argument("Messages must not be empty");
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        const auto body = Json::writeString(writer, makeRequestBody(messages, stream));
        const auto url = config.baseUrl + makeRequestPath(stream);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> client(curl_easy_init(), curl_easy_cleanup);
        if (!client)
            throw std::runtime_error("Unable to create HTTP client");

        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(nullptr, curl_slist_free_all);
        const auto addHeader = [&](const std::string &header)
        {
            auto *list = curl_slist_append(headers.get(), header.c_str());
            if (!list)
                throw std::bad_alloc();
            headers.release();
            headers.reset(list);
        };
        addHeader("Content-Type: application/json");
        addHeader(stream && getProtocol() != ApiProtocol::Ollama ? "Accept: text/event-stream" : "Accept: application/json");
        if (!config.apiKey.empty())
            addHeader(getProtocol() == ApiProtocol::Gemini ? "x-goog-api-key: " + config.apiKey : "Authorization: Bearer " + config.apiKey);

        bool finished = false;
        std::string result;
        StreamDecoder decoder(getProtocol() == ApiProtocol::Ollama, [&](const std::string &frame)
                              {
            if (frame == "[DONE]" && getProtocol() == ApiProtocol::OpenAICompatible)
            {
                finished = true;
                return;
            }
            const auto wasFinished = finished;
            const auto delta = readResponse(parseJson(frame), true, finished);
            if (wasFinished && !delta.empty()) throw std::runtime_error("Text received after stream completion");
            result += delta;
            if (!delta.empty()) callback(delta, false); });
        TransferState state;
        if (stream)
            state.receive = [&](const std::string &chunk)
            { decoder.feed(chunk); };

        // 设置总超时和连接超时，开启 HTTPS 证书验证，不跟随重定向泄露认证头。
        const auto setOption = [&](CURLoption option, auto value)
        {
            if (curl_easy_setopt(client.get(), option, value) != CURLE_OK)
                throw std::runtime_error("Unable to configure HTTP client");
        };
        setOption(CURLOPT_URL, url.c_str());
        setOption(CURLOPT_POST, 1L);
        setOption(CURLOPT_POSTFIELDS, body.c_str());
        setOption(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        setOption(CURLOPT_HTTPHEADER, headers.get());
        setOption(CURLOPT_TIMEOUT, static_cast<long>(config.timeoutSeconds));
        setOption(CURLOPT_CONNECTTIMEOUT, static_cast<long>(config.timeoutSeconds));
        setOption(CURLOPT_NOSIGNAL, 1L);
        setOption(CURLOPT_SSL_VERIFYPEER, 1L);
        setOption(CURLOPT_SSL_VERIFYHOST, 2L);
        setOption(CURLOPT_FOLLOWLOCATION, 0L);
        setOption(CURLOPT_HEADERFUNCTION, &receiveHeader);
        setOption(CURLOPT_HEADERDATA, &state);
        setOption(CURLOPT_WRITEFUNCTION, &receiveBody);
        setOption(CURLOPT_WRITEDATA, &state);
        const auto code = curl_easy_perform(client.get());
        if (state.exception)
            std::rethrow_exception(state.exception);
        if (code != CURLE_OK)
            throw std::runtime_error(std::string("HTTP transport failed: ") + curl_easy_strerror(code));
        if (state.status < 200 || state.status >= 300)
            throw std::runtime_error("Model HTTP status " + std::to_string(state.status));

        if (!stream)
            return readResponse(parseJson(state.body), false, finished);
        decoder.finish();
        if (!finished)
            throw std::runtime_error("Model stream ended without a completion marker");
        callback("", true); // 只有 HTTP 及协议均成功才通知完成，且恰好一次。
        return result;
    }

    ApiProtocol OpenAICompatibleProvider::getProtocol() const { return ApiProtocol::OpenAICompatible; }
    std::string OpenAICompatibleProvider::makeRequestPath(bool) const { return "/chat/completions"; }

    Json::Value OpenAICompatibleProvider::makeRequestBody(const std::vector<Message> &messages, bool stream) const
    {
        Json::Value body(Json::objectValue);
        body["model"] = config.model.modelName;
        body["stream"] = stream;
        body["messages"] = Json::Value(Json::arrayValue);
        for (const auto &message : messages)
        {
            Json::Value item(Json::objectValue);
            item["role"] = roleName(message.role);
            item["content"] = message.content;
            if (message.role == MessageRole::Tool)
            {
                if (message.toolCallId.empty())
                    throw std::invalid_argument("Tool result requires toolCallId");
                item["tool_call_id"] = message.toolCallId;
            }
            body["messages"].append(item);
        }
        if (config.temperature)
            body["temperature"] = *config.temperature;
        if (config.maxOutputTokens)
        {
            // OpenAI 的当前参数与兼容服务的传统参数不同，按服务平台选择。
            body[config.model.provider == ProviderType::OpenAI ? "max_completion_tokens" : "max_tokens"] = *config.maxOutputTokens;
        }
        return body;
    }

    std::string OpenAICompatibleProvider::readResponse(const Json::Value &payload, bool stream, bool &finished) const
    {
        const auto &choices = payload["choices"];
        if (!choices.isArray() || (!stream && choices.empty()))
            throw std::runtime_error("Missing chat choices");
        if (choices.empty())
            return {}; // 流末尾可能有不含候选的用量统计事件。
        const auto &choice = choices[0];
        const auto reason = choice.get("finish_reason", "").asString();
        if (reason == "tool_calls" || reason == "function_call" || reason == "content_filter")
            throw std::runtime_error("Response requires tools or was blocked; text-only interface cannot return it");
        const auto &message = choice[stream ? "delta" : "message"];
        if (!message.isObject())
            throw std::runtime_error("Missing assistant message");
        if (!message["tool_calls"].empty() || !message["function_call"].isNull())
            throw std::runtime_error("Tool calls are not supported by the text-only response interface");
        if (!stream)
            finished = true;
        return optionalText(message["content"]);
    }

    ApiProtocol GeminiProvider::getProtocol() const { return ApiProtocol::Gemini; }

    std::string GeminiProvider::makeRequestPath(bool stream) const
    {
        auto name = config.model.modelName;
        if (name.compare(0, 7, "models/") == 0)
            name.erase(0, 7);
        if (name.empty())
            throw std::invalid_argument("Gemini model name is required");
        // 对模型路径段编码，避免模型名中的斜杠或问号改变 API 路径。
        std::unique_ptr<char, decltype(&curl_free)> encoded(curl_easy_escape(nullptr, name.c_str(), 0), curl_free);
        if (!encoded)
            throw std::runtime_error("Unable to encode model name");
        return "/models/" + std::string(encoded.get()) + (stream ? ":streamGenerateContent?alt=sse" : ":generateContent");
    }

    Json::Value GeminiProvider::makeRequestBody(const std::vector<Message> &messages, bool) const
    {
        Json::Value body(Json::objectValue);
        body["contents"] = Json::Value(Json::arrayValue);
        for (const auto &message : messages)
        {
            Json::Value part(Json::objectValue);
            part["text"] = message.content;
            if (message.role == MessageRole::System)
            {
                body["systemInstruction"]["parts"].append(part);
                continue;
            }
            if (message.role != MessageRole::User && message.role != MessageRole::Assistant)
                throw std::invalid_argument("Gemini text interface supports System, User and Assistant messages");
            Json::Value item(Json::objectValue);
            item["role"] = message.role == MessageRole::Assistant ? "model" : "user";
            item["parts"].append(part);
            // 合并连续同角色消息，保持 Gemini 多轮内容的角色交替。
            auto &contents = body["contents"];
            if (!contents.empty() && contents[contents.size() - 1]["role"] == item["role"])
                contents[contents.size() - 1]["parts"].append(part);
            else
                contents.append(item);
        }
        if (body["contents"].empty())
            throw std::invalid_argument("Gemini requires a non-system message");
        if (config.temperature)
            body["generationConfig"]["temperature"] = *config.temperature;
        if (config.maxOutputTokens)
            body["generationConfig"]["maxOutputTokens"] = *config.maxOutputTokens;
        return body;
    }

    std::string GeminiProvider::readResponse(const Json::Value &payload, bool, bool &finished) const
    {
        if (payload["promptFeedback"].isMember("blockReason"))
            throw std::runtime_error("Gemini blocked the prompt");
        const auto &candidates = payload["candidates"];
        if (!candidates.isArray() || candidates.empty())
            throw std::runtime_error("Missing Gemini candidate");
        const auto &candidate = candidates[0];
        const auto reason = candidate.get("finishReason", "").asString();
        if (!reason.empty() && reason != "STOP" && reason != "MAX_TOKENS")
            throw std::runtime_error("Gemini generation was blocked or failed");
        if (!reason.empty())
            finished = true;
        const auto &parts = candidate["content"]["parts"];
        if (!parts.isNull() && !parts.isArray())
            throw std::runtime_error("Invalid Gemini parts");
        std::string text;
        for (const auto &part : parts)
        {
            if (part.isMember("functionCall"))
                throw std::runtime_error("Gemini function calls require a tool interface");
            if (!part.get("thought", false).asBool())
                text += optionalText(part["text"]);
        }
        return text;
    }

    ApiProtocol OllamaProvider::getProtocol() const { return ApiProtocol::Ollama; }
    std::string OllamaProvider::makeRequestPath(bool) const
    {
        return config.baseUrl.size() >= 4 && config.baseUrl.compare(config.baseUrl.size() - 4, 4, "/api") == 0 ? "/chat" : "/api/chat";
    }

    Json::Value OllamaProvider::makeRequestBody(const std::vector<Message> &messages, bool stream) const
    {
        Json::Value body(Json::objectValue);
        body["model"] = config.model.modelName;
        body["stream"] = stream;
        body["messages"] = Json::Value(Json::arrayValue);
        for (const auto &message : messages)
        {
            if (message.role == MessageRole::Tool)
                throw std::invalid_argument("Ollama tool results require tool definitions and names, not provided by the text interface");
            Json::Value item(Json::objectValue);
            item["role"] = roleName(message.role);
            item["content"] = message.content;
            body["messages"].append(item);
        }
        if (config.temperature)
            body["options"]["temperature"] = *config.temperature;
        if (config.maxOutputTokens)
            body["options"]["num_predict"] = *config.maxOutputTokens;
        return body;
    }

    std::string OllamaProvider::readResponse(const Json::Value &payload, bool stream, bool &finished) const
    {
        if (!payload["done"].isBool())
            throw std::runtime_error("Missing Ollama completion state");
        finished = payload["done"].asBool();
        if (!stream && !finished)
            throw std::runtime_error("Incomplete non-streaming Ollama response");
        if (!payload["message"].isObject())
            throw std::runtime_error("Missing Ollama message");
        if (!payload["message"]["tool_calls"].empty())
            throw std::runtime_error("Ollama tool calls require a tool interface");
        return optionalText(payload["message"]["content"]);
    }
}

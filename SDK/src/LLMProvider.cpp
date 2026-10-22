#include "LLMProvider.hpp"
#include "Util.hpp"
#include <httplib.h>
#include <json/json.h>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace modelBridge
{
    // 基类：初始化、认证和公共 HTTP 流程
    bool LLMProvider::initialize(const ModelConfig &modelConfig)
    {
        configurationValid = false;
        lastError.clear();
        try
        {
            config = modelConfig;
            if (config.model.protocol != getProtocol() || config.model.modelName.empty())
                throw std::invalid_argument("Matching protocol and model name are required");
            const auto scheme = config.baseUrl.find("://");
            if (scheme == std::string::npos ||
                (config.baseUrl.substr(0, scheme) != "http" && config.baseUrl.substr(0, scheme) != "https") ||
                config.baseUrl.find_first_of("?#@\r\n\t ") != std::string::npos)
                throw std::invalid_argument("baseUrl must be an HTTP(S) service root without credentials or query");
            while (!config.baseUrl.empty() && config.baseUrl.back() == '/')
                config.baseUrl.pop_back();
            if (config.timeoutSeconds <= 0 || (config.maxOutputTokens && *config.maxOutputTokens <= 0))
                throw std::invalid_argument("Timeout and output token limit must be positive");
            if (config.temperature && (!std::isfinite(*config.temperature) || *config.temperature < 0))
                throw std::invalid_argument("Temperature must be finite and non-negative");

            validateReasoning(config);

            if (config.apiKey.empty() && !config.apiKeyEnv.empty())
            {
                if (const auto *key = std::getenv(config.apiKeyEnv.c_str()))
                    config.apiKey = key;
            }

            if (getProtocol() != ApiProtocol::Ollama && config.apiKey.empty())
                throw std::invalid_argument("API key is required for this protocol");

            if (config.apiKey.find_first_of("\r\n") != std::string::npos)
                throw std::invalid_argument("API key contains a newline");

            configurationValid = true;

            MB_LOG_INFO("模型初始化成功：model={}", config.model.modelName);
            resetProtocol();
            clearHistory(); // 重新初始化成功后开始新对话，保留系统提示词。
            return true;
        }
        catch (const std::exception &error)
        {
            configurationValid = false;
            lastError = error.what();
            MB_LOG_ERROR("模型初始化失败：{}", lastError);
            return false;
        }
    }

    bool LLMProvider::isInitialized() const
    {
        return configurationValid;
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
        MB_LOG_DEBUG("模型认证信息已更新");
    }

    void LLMProvider::setSystemPrompt(const std::string &prompt)
    {
        systemPrompt = prompt;
    }

    void LLMProvider::clearHistory()
    {
        history.clear();
    }

    const std::vector<Message> &LLMProvider::getHistory() const
    {
        return history;
    }

    std::vector<Message> LLMProvider::buildMessages(const std::string &message) const
    {
        // 在副本上构造本轮消息，请求失败不会留下尚未完成的用户消息。
        auto messages = history;
        if (!systemPrompt.empty())
            messages.insert(messages.begin(), Message{MessageRole::System, systemPrompt, currentTimeMillis(), {}, {}});
        messages.push_back({MessageRole::User, message, currentTimeMillis(), {}, {}});
        return messages;
    }

    void LLMProvider::setTools(const std::vector<Tool> &value)
    {
        tools = value;
    }

    ChatResult LLMProvider::sendMessage(const std::string &message)
    {
        return performRequest(message, false, {});
    }

    ChatResult LLMProvider::sendMessageStream(const std::string &message, const StreamCallback &callback)
    {
        if (!callback)
            throw std::invalid_argument("Stream callback is required");
        auto result = performRequest(message, true, callback);
        callback("", true); // 整条工具链保存后，只通知一次完成。
        return result;
    }

    void LLMProvider::saveHistory(const std::vector<Message> &messages)
    {
        auto begin = messages.begin();
        if (begin != messages.end() && begin->role == MessageRole::System) // 不保存系统消息
            ++begin;
        std::vector<Message> saved(begin, messages.end());
        history.swap(saved);
    }

    ChatResult LLMProvider::performRequest(const std::string &message, bool stream, const StreamCallback &callback)
    {
        auto messages = buildMessages(message);
        ChatResult result;
        result.messages.push_back(messages.back());
        for (;;)
        {
            auto reply = requestOnce(messages, stream, callback);
            messages.push_back(reply);
            result.messages.push_back(reply);
            result.content += reply.content;
            saveHistory(messages);
            if (reply.toolCalls.empty())
                return result;

            // 每轮先执行全部工具，再将全部结果一起交回模型。
            for (const auto &call : reply.toolCalls)
            {
                Message output{MessageRole::Tool, {}, currentTimeMillis(), call.id, call.name};
                const auto tool = std::find_if(tools.begin(), tools.end(), [&](const Tool &entry)
                                               { return entry.name == call.name; });
                try
                {
                    if (tool == tools.end() || !tool->execute)
                        output.content = "{\"error\":\"Tool is not registered\"}";
                    else
                        output.content = tool->execute(call.arguments);
                }
                catch (const std::exception &error)
                {
                    Json::Value failure;
                    failure["error"] = error.what();
                    output.content = compactJson(failure);
                }
                messages.push_back(output);
                result.messages.push_back(output);
                // 已执行的工具不因后续 HTTP 失败而从历史消失，避免重试时重复执行
                saveHistory(messages);
            }
        }
    }

    Message LLMProvider::requestOnce(const std::vector<Message> &messages, bool stream, const StreamCallback &callback)
    {
        const auto started = std::chrono::steady_clock::now();
        const char *stage = "检查配置";
        std::string errorBody;
        long status = 0;
        httplib::Error transportError = httplib::Error::Success;
        try
        {
            if (!isInitialized())
                throw std::logic_error("Provider is not initialized");
            if (messages.empty())
                throw std::invalid_argument("Messages must not be empty");
            MB_LOG_INFO("开始模型请求：model={}，stream={}，消息数={}", config.model.modelName, stream, messages.size());

            // 1. httplib 的 Client 只接收协议和主机，版本前缀留在请求路径中。
            stage = "构造请求";
            const auto protocol = getProtocol();
            const auto pathStart = config.baseUrl.find('/', config.baseUrl.find("://") + 3);
            const auto origin = config.baseUrl.substr(0, pathStart);
            const auto prefix = pathStart == std::string::npos ? "" : config.baseUrl.substr(pathStart);
            httplib::Client client(origin);
            client.set_connection_timeout(config.timeoutSeconds);
            client.set_read_timeout(config.timeoutSeconds);
            client.set_write_timeout(config.timeoutSeconds);
            client.set_max_timeout(std::chrono::seconds(config.timeoutSeconds));

            httplib::Request request;
            request.method = "POST";
            request.path = prefix + makeRequestPath(stream);
            request.headers.emplace("Content-Type", "application/json");
            request.headers.emplace("Accept", stream && protocol != ApiProtocol::Ollama ? "text/event-stream" : "application/json");
            if (!config.apiKey.empty())
            {
                const auto header = protocol == ApiProtocol::Gemini ? "x-goog-api-key" : "Authorization";
                request.headers.emplace(header, protocol == ApiProtocol::Gemini ? config.apiKey : "Bearer " + config.apiKey);
            }
            Json::StreamWriterBuilder writer;
            writer["indentation"] = ""; // 不对 JSON 进行格式化缩进，减少不必要的请求体字节
            request.body = Json::writeString(writer, makeRequestBody(messages, stream));
            MB_LOG_DEBUG("模型请求体已构造：字节数={}", request.body.size());

            // 2. 流式事件只回调文本增量；外层保存历史后再发出完成通知。
            bool finished = false;
            bool receivedDone = false;
            Message result{MessageRole::Assistant, {}, currentTimeMillis(), {}, {}};
            StreamDecoder decoder;
            decoder.ndjson = protocol == ApiProtocol::Ollama;
            if (stream)
            {
                decoder.onEvent = [&](const std::string &event)
                {
                    if (protocol == ApiProtocol::OpenAICompatible && event == "[DONE]")
                    {
                        if (receivedDone)
                            throw std::runtime_error("Duplicate stream completion marker");

                        receivedDone = true;
                        return;
                    }

                    if (receivedDone)
                        throw std::runtime_error("Data received after stream completion");

                    const auto delta = readResponse(parseJson(event), true, result, finished);
                    result.content += delta;

                    if (!delta.empty())
                        callback(delta, false);
                };
            }

            request.response_handler = [&](const httplib::Response &response)
            {
                status = response.status;
                return true;
            };

            if (stream)
            {
                request.content_receiver = [&](const char *data, size_t length, size_t, size_t)
                {
                    if (status >= 200 && status < 300)
                        decoder.append(data, length);
                    else
                        errorBody.append(data, length);

                    return true;
                };
            }
            stage = "传输或流式处理";
            const auto response = client.send(request);
            transportError = response.error();
            if (!response)
                throw std::runtime_error("HTTP request failed: " + httplib::to_string(transportError));
            status = response->status;
            if (status < 200 || status >= 300)
            {
                const auto &body = stream ? errorBody : response->body;
                Json::Value error;
                Json::CharReaderBuilder reader;
                std::istringstream input(body);
                std::string diagnostics;
                // 只在 HTTP 明确拒绝当前协议时回退；传输失败、鉴权和限流不重发。
                if ((status == 400 || status == 404 || status == 405) &&
                    Json::parseFromStream(reader, input, &error, &diagnostics) && tryProtocolFallback(error))
                    return requestOnce(messages, stream, callback);
                throw std::runtime_error("Model HTTP status " + std::to_string(status) + ": " + body);
            }

            // 3. 解析剩余数据。补入换行，用同一分帧逻辑处理末尾没有换行的数据。
            stage = "解析回复或完成回调";
            if (stream)
            {
                decoder.finish(); // 处理缓冲区尚未解析的数据
                if (!finished || (requiresDoneMarker() && !receivedDone))
                {
                    throw std::runtime_error("Model stream ended without complete termination information");
                }
            }
            else
                result.content = readResponse(parseJson(response->body), false, result, finished);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            MB_LOG_INFO("模型请求完成：model={}，HTTP={}，回复字节数={}，耗时={}ms", config.model.modelName, status, result.content.size(), elapsed);
            return result;
        }
        catch (...)
        {
            MB_LOG_ERROR("模型请求失败：model={}，阶段={}，HTTP={}，传输状态={}", config.model.modelName, stage, status, httplib::to_string(transportError));
            throw;
        }
    }

    // OpenAI 兼容协议：OpenAI、DeepSeek、百炼
    ApiProtocol OpenAICompatibleProvider::getProtocol() const { return ApiProtocol::OpenAICompatible; }

    std::string OpenAICompatibleProvider::makeRequestPath(bool) const
    {
        return responses ? "/responses" : "/chat/completions";
    }

    bool OpenAICompatibleProvider::tryProtocolFallback(const Json::Value &error)
    {
        if (responses || !error.isObject() || !error["error"].isObject())
            return false;

        const auto &codeValue = error["error"]["code"];
        if (!codeValue.isString())
            return false;

        const auto code = codeValue.asString();
        if (code != "protocol_not_supported" && code != "unsupported_protocol" && code != "unsupported_endpoint")
        {
            return false;
        }

        responses = true;
        MB_LOG_INFO("当前渠道不支持 Chat Completions，改用 Responses");
        return true;
    }

    Json::Value OpenAICompatibleProvider::makeRequestBody(const std::vector<Message> &messages, bool stream) const
    {
        Json::Value body;
        body["model"] = config.model.modelName;
        body["stream"] = stream;
        if (config.temperature)
            body["temperature"] = *config.temperature;
        if (!responses)
        {
            body["messages"] = makeChatMessages(messages, getProtocol());
            if (!tools.empty())
                body["tools"] = chatTools(tools);
            if (config.maxOutputTokens)
                body[config.model.provider == ProviderType::OpenAI ? "max_completion_tokens" : "max_tokens"] = *config.maxOutputTokens;
            if (config.reasoningEffort)
                body["reasoning_effort"] = *config.reasoningEffort;
            if (config.thinkingEnabled.has_value())
            {
                if (config.model.provider == ProviderType::DeepSeek)
                    body["thinking"]["type"] = *config.thinkingEnabled ? "enabled" : "disabled";
                else
                    body["enable_thinking"] = *config.thinkingEnabled;
            }
            return body;
        }

        body["store"] = false; // 历史由 SDK 管理，不依赖服务端会话。
        body["include"].append("reasoning.encrypted_content");
        body["input"] = Json::Value(Json::arrayValue);
        for (const auto &message : messages)
        {
            if (message.role == MessageRole::Assistant && !message.protocolData.empty())
            {
                const auto native = parseJson(message.protocolData, false);
                for (const auto &item : native["output"])
                    body["input"].append(item);
                continue;
            }
            Json::Value item;
            if (message.role == MessageRole::Tool)
            {
                item["type"] = "function_call_output";
                item["call_id"] = message.toolCallId;
                item["output"] = message.content;
                body["input"].append(item);
                continue;
            }
            if (!message.content.empty() || message.toolCalls.empty())
            {
                item["role"] = roleName(message.role);
                item["content"] = message.content;
                body["input"].append(item);
            }
            for (const auto &call : message.toolCalls)
            {
                Json::Value function;
                function["type"] = "function_call";
                function["call_id"] = call.id;
                function["name"] = call.name;
                function["arguments"] = call.arguments;
                body["input"].append(function);
            }
        }
        if (!tools.empty())
        {
            body["tools"] = Json::Value(Json::arrayValue);
            for (const auto &tool : tools)
            {
                auto entry = functionSchema(tool);
                entry["type"] = "function";
                entry["strict"] = false;
                body["tools"].append(entry);
            }
        }
        if (config.maxOutputTokens)
            body["max_output_tokens"] = *config.maxOutputTokens;
        if (config.reasoningEffort)
            body["reasoning"]["effort"] = *config.reasoningEffort;
        if (config.thinkingEnabled.has_value())
        {
            if (config.model.provider == ProviderType::DeepSeek)
                body["reasoning"]["effort"] = *config.thinkingEnabled ? config.reasoningEffort.value_or("high") : "none";
            else
                body["enable_thinking"] = *config.thinkingEnabled; // 百炼扩展
        }
        return body;
    }

    std::string OpenAICompatibleProvider::readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished) const
    {
        if (responses)
        {
            const auto type = payload.get("type", "").asString();
            if (stream && type == "response.output_text.delta")
                return readText(payload["delta"]);
            if (stream && type != "response.completed" && type != "response.incomplete" && type != "response.failed")
                return {}; // 最终 response.output 包含完整调用参数和原生推理项。
            const auto &response = stream ? payload["response"] : payload;
            const auto status = response.get("status", "").asString();
            if (status != "completed" && status != "incomplete")
                throw std::runtime_error("Responses request did not complete successfully");
            const auto text = readResponseOutput(response["output"], reply);
            if (status == "incomplete" && !reply.toolCalls.empty())
                throw std::runtime_error("Tool call was truncated");
            finished = true;
            return stream && !reply.content.empty() ? std::string{} : text;
        }

        const auto &choices = payload["choices"];
        if (!choices.isArray() || (!stream && choices.empty()))
            throw std::runtime_error("Missing chat choices");
        if (choices.empty())
            return {};
        const auto &message = choices[0][stream ? "delta" : "message"];
        const auto reason = choices[0].get("finish_reason", "").asString();
        if (!message.isObject())
            throw std::runtime_error("Missing assistant message");
        if (reason == "content_filter")
            throw std::runtime_error("Response was blocked");
        readToolCalls(message["tool_calls"], stream, false, reply);
        reply.reasoningContent += readText(message["reasoning_content"]);
        if (reason == "length")
        {
            if (!reply.toolCalls.empty())
                throw std::runtime_error("Tool call was truncated");
            MB_LOG_WARN("模型回复达到输出上限：model={}", config.model.modelName);
        }
        if (!reason.empty())
        {
            if (reason != "stop" && reason != "length" && reason != "tool_calls")
                throw std::runtime_error("Unsupported finish_reason: " + reason);
            finished = true;
        }
        else if (!stream)
            throw std::runtime_error("Non-streaming response is missing finish_reason");
        return readText(message["content"]) + readText(message["refusal"]);
    }

    // 2. Gemini 原生协议
    ApiProtocol GeminiProvider::getProtocol() const { return ApiProtocol::Gemini; }

    /// 模型名放入 URL；流式使用 streamGenerateContent 和 SSE。
    std::string GeminiProvider::makeRequestPath(bool stream) const
    {
        auto model = config.model.modelName;
        if (model.compare(0, 7, "models/") == 0)
            model.erase(0, 7);
        if (model.empty())
            throw std::invalid_argument("Gemini model name is required");
        return "/models/" + httplib::encode_uri_component(model) +
               (stream ? ":streamGenerateContent?alt=sse" : ":generateContent");
    }

    /// System 放入 systemInstruction，Assistant 转为 model，其余文本放入 parts。
    Json::Value GeminiProvider::makeRequestBody(const std::vector<Message> &messages, bool) const
    {
        Json::Value body;
        auto &contents = body["contents"];
        contents = Json::Value(Json::arrayValue);
        for (const auto &message : messages)
        {
            if (message.role == MessageRole::System)
            {
                Json::Value part;
                part["text"] = message.content;
                body["systemInstruction"]["parts"].append(part);
                continue;
            }
            Json::Value parts(Json::arrayValue);
            if (message.role == MessageRole::Tool)
            {
                Json::Value part;
                part["functionResponse"]["name"] = message.toolName;
                part["functionResponse"]["response"]["result"] = message.content;
                if (!message.toolCallId.empty())
                    part["functionResponse"]["id"] = message.toolCallId;
                parts.append(part);
            }
            else if (message.role == MessageRole::Assistant && !message.protocolData.empty())
                parts = parseJson(message.protocolData, false)["parts"];
            else
            {
                if (!message.content.empty())
                {
                    Json::Value part;
                    part["text"] = message.content;
                    parts.append(part);
                }
                for (const auto &call : message.toolCalls)
                {
                    Json::Value part;
                    part["functionCall"]["name"] = call.name;
                    part["functionCall"]["args"] = parseJson(call.arguments, false);
                    if (!call.id.empty())
                        part["functionCall"]["id"] = call.id;
                    parts.append(part);
                }
            }
            const auto role = message.role == MessageRole::Assistant ? "model" : "user";
            if (!contents.empty() && contents[contents.size() - 1]["role"] == role)
            {
                for (const auto &part : parts)
                    contents[contents.size() - 1]["parts"].append(part);
            }
            else
            {
                Json::Value item;
                item["role"] = role;
                item["parts"] = parts;
                contents.append(item);
            }
        }
        if (!tools.empty())
        {
            Json::Value declarations;
            for (const auto &tool : tools)
                declarations["functionDeclarations"].append(functionSchema(tool));
            body["tools"].append(declarations);
        }
        if (contents.empty())
            throw std::invalid_argument("Gemini requires a non-system message");
        if (config.temperature)
            body["generationConfig"]["temperature"] = *config.temperature;
        if (config.maxOutputTokens)
            body["generationConfig"]["maxOutputTokens"] = *config.maxOutputTokens;
        if (config.reasoningEffort)
        {
            auto level = *config.reasoningEffort;
            for (auto &letter : level)
                if (letter >= 'a' && letter <= 'z')
                    letter = static_cast<char>(letter - 'a' + 'A');
            body["generationConfig"]["thinkingConfig"]["thinkingLevel"] = level;
        }
        return body;
    }

    /// 按顺序拼接首个候选的可见 parts，以 finishReason 判断流结束。
    std::string GeminiProvider::readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished) const
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
        if (reason == "MAX_TOKENS")
            MB_LOG_WARN("Gemini 回复达到输出上限：model={}", config.model.modelName);
        if (!reason.empty())
            finished = true;
        const auto &parts = candidate["content"]["parts"];
        if (!parts.isNull() && !parts.isArray())
            throw std::runtime_error("Invalid Gemini parts");
        auto native = reply.protocolData.empty() ? Json::Value(Json::objectValue) : parseJson(reply.protocolData, false);
        std::string text;
        for (const auto &part : parts)
        {
            native["parts"].append(part); // 原样保留签名、思考 part 和 functionCall。
            if (part.isMember("functionCall"))
            {
                const auto &call = part["functionCall"];
                reply.toolCalls.push_back({call.get("id", "").asString(), call["name"].asString(), compactJson(call.get("args", Json::Value(Json::objectValue)))});
            }
            else if (part.get("thought", false).asBool())
                reply.reasoningContent += readText(part["text"]);
            else
                text += readText(part["text"]);
        }
        reply.protocolData = compactJson(native);
        if (reason == "MAX_TOKENS" && !reply.toolCalls.empty())
            throw std::runtime_error("Tool call was truncated");
        return text;
    }

    // 3. Ollama 原生协议
    ApiProtocol OllamaProvider::getProtocol() const { return ApiProtocol::Ollama; }

    /// 兼容 baseUrl 已带 /api 的写法，避免重复拼接。
    std::string OllamaProvider::makeRequestPath(bool) const
    {
        const bool hasApi = config.baseUrl.size() >= 4 && config.baseUrl.compare(config.baseUrl.size() - 4, 4, "/api") == 0;
        return hasApi ? "/chat" : "/api/chat";
    }

    /// 采样参数放入 options；think 在顶层，支持字符串级别或布尔开关。
    Json::Value OllamaProvider::makeRequestBody(const std::vector<Message> &messages, bool stream) const
    {
        Json::Value body;
        body["model"] = config.model.modelName;
        body["messages"] = makeChatMessages(messages, getProtocol());
        body["stream"] = stream;
        if (!tools.empty())
            body["tools"] = chatTools(tools);
        if (config.temperature)
            body["options"]["temperature"] = *config.temperature;
        if (config.maxOutputTokens)
            body["options"]["num_predict"] = *config.maxOutputTokens;
        if (config.reasoningEffort)
            body["think"] = *config.reasoningEffort;
        else if (config.thinkingEnabled.has_value())
            body["think"] = *config.thinkingEnabled;
        return body;
    }

    /// 普通 JSON 和每条 NDJSON 使用同一结构；最终记录可同时带文本和 done=true。
    std::string OllamaProvider::readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished) const
    {
        const auto &message = payload["message"];
        if (!payload["done"].isBool() || !message.isObject())
            throw std::runtime_error("Invalid Ollama response");
        finished = payload["done"].asBool();
        if (!stream && !finished)
            throw std::runtime_error("Incomplete non-streaming Ollama response");
        readToolCalls(message["tool_calls"], stream, true, reply);
        reply.reasoningContent += readText(message["thinking"]);
        if (finished && payload.get("done_reason", "").asString() == "length")
        {
            if (!reply.toolCalls.empty())
                throw std::runtime_error("Tool call was truncated");
            MB_LOG_WARN("Ollama 回复达到输出上限：model={}", config.model.modelName);
        }
        return readText(message["content"]);
    }
} // namespace modelBridge

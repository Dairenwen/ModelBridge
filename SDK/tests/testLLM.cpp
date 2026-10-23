#include "LLMProvider.hpp"
#include <gtest/gtest.h>
#include <json/json.h>
#include <iostream>
#include <sstream>
#include <string>

using namespace modelBridge;

namespace
{
    // OpenAICompatibleProvider provider;
    OllamaProvider provider;
    using TestProvider = decltype(provider); // 思考测试跟随主测试使用的 Provider 类型。
    ModelConfig testConfig;                  // 在 main 中保存完整配置，供不同思考模式的测试复用

    void expectThinking(const Json::Value &request, bool enabled)
    {
        if (testConfig.model.protocol == ApiProtocol::Ollama)
        {
            ASSERT_TRUE(request["think"].isBool());
            EXPECT_EQ(request["think"].asBool(), enabled);
        }
        else if (testConfig.model.provider == ProviderType::Bailian)
        {
            ASSERT_TRUE(request["enable_thinking"].isBool());
            EXPECT_EQ(request["enable_thinking"].asBool(), enabled);
        }
        else
            EXPECT_EQ(request["thinking"]["type"].asString(), enabled ? "enabled" : "disabled");
    }

    // 仅在测试中观察请求字段和思考内容长度，仍使用 SDK 原有的构造、解析和 HTTP 流程。
    class ThinkingTestProvider : public TestProvider
    {
    public:
        mutable Json::Value lastRequest;
        mutable std::size_t thinkingBytes = 0;

    protected:
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const override
        {
            lastRequest = TestProvider::makeRequestBody(messages, stream, context);
            return lastRequest;
        }

        std::string readResponse(const Json::Value &payload, bool stream, Message &replyMessage, bool &finished, const RequestContext &context) const override
        {
            const auto previousSize = replyMessage.reasoningContent.size();
            auto reply = TestProvider::readResponse(payload, stream, replyMessage, finished, context);
            // 各协议解析后统一保存至 reasoningContent，只统计本次新增内容。
            thinkingBytes += replyMessage.reasoningContent.size() - previousSize;
            return reply;
        }
    };

    TEST(LLMTest, Initialization)
    {
        EXPECT_TRUE(provider.isInitialized());
    }

    TEST(LLMTest, FullResponse)
    {
        provider.clearHistory(); // 每项测试独立；第二次请求自动携带第一轮问答。
        const auto code = "MB_FULL_" + std::to_string(currentTimeMillis());
        const auto firstMessage = "请记住本轮对话的代号：" + code + "。只回答收到。";
        const std::string secondMessage = "上一条用户消息中的代号是什么？只返回该代号。";
        const auto firstReply = provider.sendMessage(firstMessage);
        ASSERT_FALSE(firstReply.content.empty());
        const auto reply = provider.sendMessage(secondMessage);

        EXPECT_NE(reply.content.find(code), std::string::npos); // 当前消息未重复代号，回答需依赖历史。
        const auto &history = provider.getHistory();
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history[0].role, MessageRole::User);
        EXPECT_EQ(history[0].content, firstMessage);
        EXPECT_EQ(history[1].role, MessageRole::Assistant);
        EXPECT_EQ(history[1].content, firstReply.content);
        EXPECT_EQ(history[2].role, MessageRole::User);
        EXPECT_EQ(history[2].content, secondMessage);
        EXPECT_EQ(history[3].role, MessageRole::Assistant);
        EXPECT_EQ(history[3].content, reply.content);
        std::cout << "全量回复：" << reply.content << '\n';
    }

    TEST(LLMTest, StreamResponse)
    {
        provider.clearHistory();
        const auto code = "MB_STREAM_" + std::to_string(currentTimeMillis());
        const auto firstMessage = "请记住本轮对话的代号：" + code + "。只回答收到。";
        const std::string secondMessage = "上一条用户消息中的代号是什么？只返回该代号。";
        const auto firstReply = provider.sendMessageStream(firstMessage, [](const std::string &, bool) {});
        ASSERT_FALSE(firstReply.content.empty());
        std::string received;
        int doneCount = 0;
        std::cout << "流式回复：" << std::flush;

        const auto reply = provider.sendMessageStream(
            secondMessage, [&](const std::string &delta, bool done)
            {
                EXPECT_EQ(doneCount, 0) << "结束后不应继续回调";
                received += delta;
                std::cout << delta << std::flush;
                if (done)
                {
                    ++doneCount;
                    EXPECT_EQ(provider.getHistory().size(), 4u); // 结束回调前已经保存本轮问答。
                } });
        std::cout << '\n';

        EXPECT_NE(reply.content.find(code), std::string::npos);
        EXPECT_EQ(reply.content, received);
        EXPECT_EQ(doneCount, 1);
        const auto &history = provider.getHistory();
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history[0].role, MessageRole::User);
        EXPECT_EQ(history[0].content, firstMessage);
        EXPECT_EQ(history[1].role, MessageRole::Assistant);
        EXPECT_EQ(history[1].content, firstReply.content);
        EXPECT_EQ(history[2].role, MessageRole::User);
        EXPECT_EQ(history[2].content, secondMessage);
        EXPECT_EQ(history[3].role, MessageRole::Assistant);
        EXPECT_EQ(history[3].content, reply.content);
    }

    TEST(LLMTest, ThinkingMode)
    {
        // 只改变开关，不传推理深度；开启后由服务端选择默认深度。
        for (const bool enabled : {false, true})
        {
            SCOPED_TRACE(enabled ? "thinking enabled" : "thinking disabled");
            auto options = testConfig;
            options.thinkingEnabled = enabled;
            options.reasoningEffort.reset();

            ThinkingTestProvider thinkingProvider;
            ASSERT_TRUE(thinkingProvider.initialize(options)) << thinkingProvider.getLastError();
            const auto reply = thinkingProvider.sendMessage("一个数加 5 后乘 3 等于 36，求这个数。只回答数字。");
            EXPECT_NE(reply.content.find("7"), std::string::npos);
            expectThinking(thinkingProvider.lastRequest, enabled);
            EXPECT_FALSE(thinkingProvider.lastRequest.isMember("reasoning_effort"));
            EXPECT_EQ(thinkingProvider.thinkingBytes > 0, enabled);
            std::cout << "thinkingEnabled=" << std::boolalpha << enabled << "，全量思考字节数=" << thinkingProvider.thinkingBytes << '\n';

            thinkingProvider.thinkingBytes = 0;
            std::string received;
            int doneCount = 0;
            const auto streamReply = thinkingProvider.sendMessageStream(
                "将上一轮的结果乘 2，只回答数字。", [&](const std::string &delta, bool done)
                {
                    received += delta;
                    if (done)
                        ++doneCount; });
            EXPECT_NE(streamReply.content.find("14"), std::string::npos);
            EXPECT_EQ(streamReply.content, received);
            EXPECT_EQ(doneCount, 1);
            EXPECT_FALSE(thinkingProvider.lastRequest.isMember("reasoning_effort"));
            EXPECT_EQ(thinkingProvider.thinkingBytes > 0, enabled);
            EXPECT_EQ(thinkingProvider.getHistory().size(), 4u);
            std::cout << "thinkingEnabled=" << enabled << "，流式思考字节数=" << thinkingProvider.thinkingBytes << '\n';
        }
    }

    TEST(LLMTest, ReasoningEffort)
    {
        const bool ollama = testConfig.model.protocol == ApiProtocol::Ollama;
        const std::vector<std::string> efforts = ollama ? std::vector<std::string>{"low", "xhigh"} : std::vector<std::string>{"low", "high"};
        const char *effortField = ollama ? "think" : "reasoning_effort";
        for (const auto &effort : efforts)
        {
            SCOPED_TRACE(effort);
            auto options = testConfig;
            options.reasoningEffort = effort;
            options.thinkingEnabled.reset();
            if (!ollama && (options.model.provider == ProviderType::DeepSeek ||
                            options.model.provider == ProviderType::Bailian))
                options.thinkingEnabled = true;

            ThinkingTestProvider thinkingProvider;
            ASSERT_TRUE(thinkingProvider.initialize(options)) << thinkingProvider.getLastError();
            thinkingProvider.setSystemPrompt("请简短回答。");
            const auto reply = thinkingProvider.sendMessage("7 加 8 等于多少？只回答数字。");
            EXPECT_NE(reply.content.find("15"), std::string::npos);
            if (options.thinkingEnabled.has_value())
                expectThinking(thinkingProvider.lastRequest, true);
            EXPECT_EQ(thinkingProvider.lastRequest[effortField].asString(), effort);

            std::string received;
            int doneCount = 0;
            const auto streamReply = thinkingProvider.sendMessageStream(
                "将上一轮的计算结果加 1，只回答数字。", [&](const std::string &delta, bool done)
                {
                    received += delta;
                    if (done)
                        ++doneCount; });
            EXPECT_NE(streamReply.content.find("16"), std::string::npos);
            EXPECT_EQ(streamReply.content, received);
            EXPECT_EQ(doneCount, 1);
            EXPECT_EQ(thinkingProvider.lastRequest[effortField].asString(), effort);
            EXPECT_EQ(thinkingProvider.getHistory().size(), 4u);
        }
    }

    TEST(LLMTest, OllamaToolCalls)
    {
        if (testConfig.model.protocol != ApiProtocol::Ollama)
            GTEST_SKIP() << "此用例连接本地 Ollama 测试真实工具调用。";

        for (const bool stream : {false, true})
        {
            SCOPED_TRACE(stream ? "stream tools" : "full tools");
            OllamaProvider toolProvider;
            ASSERT_TRUE(toolProvider.initialize(testConfig)) << toolProvider.getLastError();
            std::vector<std::string> executions;
            const std::string parameters = R"({"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false})";
            toolProvider.setTools({{"add", "计算 a 与 b 的和。", parameters,
                                    [&](const std::string &arguments)
                                    {
                                        Json::Value args;
                                        std::istringstream input(arguments);
                                        input >> args;
                                        executions.push_back("add");
                                        EXPECT_DOUBLE_EQ(args["a"].asDouble() + args["b"].asDouble(), 20.0);
                                        const auto output = std::to_string(args["a"].asDouble() + args["b"].asDouble());
                                        std::cout << "本地执行 add：" << arguments << " -> " << output << std::endl;
                                        return output;
                                    }},
                                   {"multiply", "计算 a 与 b 的乘积。", parameters,
                                    [&](const std::string &arguments)
                                    {
                                        Json::Value args;
                                        std::istringstream input(arguments);
                                        input >> args;
                                        executions.push_back("multiply");
                                        const double a = args["a"].asDouble(), b = args["b"].asDouble();
                                        EXPECT_TRUE((a == 20.0 && b == 3.0) || (a == 3.0 && b == 20.0));
                                        const auto output = std::to_string(a * b);
                                        std::cout << "本地执行 multiply：" << arguments << " -> " << output << std::endl;
                                        return output;
                                    }}});

            const std::string prompt = "请调用工具计算 (12 + 8) × 3。先只调用 add，收到它的结果后，"
                                       "再调用 multiply 将结果乘以 3。最后只回答最终数字。";
            std::string received;
            int doneCount = 0;
            const auto result = stream ? toolProvider.sendMessageStream(
                                             prompt, [&](const std::string &delta, bool done)
                                             {
                    EXPECT_EQ(doneCount, 0);
                    received += delta;
                    if (done)
                        ++doneCount; })
                                       : toolProvider.sendMessage(prompt);

            EXPECT_EQ(executions, (std::vector<std::string>{"add", "multiply"}));
            // 两次模型调用分别请求工具，中间确实保存并回传了第一次工具结果。
            ASSERT_EQ(result.messages.size(), 6u);
            ASSERT_EQ(toolProvider.getHistory().size(), result.messages.size());
            EXPECT_EQ(result.messages[0].role, MessageRole::User);
            for (std::size_t index = 0; index < 2; ++index)
            {
                const auto &request = result.messages[1 + index * 2];
                const auto &output = result.messages[2 + index * 2];
                EXPECT_EQ(request.role, MessageRole::Assistant);
                ASSERT_EQ(request.toolCalls.size(), 1u);
                EXPECT_EQ(request.toolCalls[0].name, index == 0 ? "add" : "multiply");
                EXPECT_EQ(output.role, MessageRole::Tool);
                EXPECT_EQ(output.toolName, request.toolCalls[0].name);
                EXPECT_EQ(output.toolCallId, request.toolCalls[0].id);
                EXPECT_DOUBLE_EQ(std::stod(output.content), index == 0 ? 20.0 : 60.0);
                EXPECT_EQ(toolProvider.getHistory()[2 + index * 2].content, output.content);
            }
            EXPECT_EQ(result.messages.back().role, MessageRole::Assistant);
            EXPECT_EQ(result.messages.back().content, "60");
            if (stream)
            {
                EXPECT_EQ(received, result.content);
                EXPECT_EQ(doneCount, 1);
            }
            std::cout << (stream ? "流式" : "全量") << "工具链最终回复：" << result.content << std::endl;

            toolProvider.setTools({});
            const auto followup = toolProvider.sendMessage("上一轮最终的计算结果是多少？只回答数字。");
            EXPECT_EQ(followup.content, "60");
            EXPECT_EQ(executions.size(), 2u); // 回放历史没有重复执行本地工具。
        }
    }
} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    // ModelConfig config{};
    // config.model.modelName = "gpt-6.1-sol";
    // config.model.protocol = ApiProtocol::OpenAICompatible;
    // config.model.provider = ProviderType::Custom;
    // config.baseUrl = "https://cf.api.fan/v1";
    // config.apiKey = "sk-WZ0Mu6ZiyGgzlmE5Xx9f53bv6Yg8Wf5RwSvNKJqdYTlwfJ1M";
    // config.apiKeyEnv = "OPENAI_API_KEY";
    // config.timeoutSeconds = 120;
    // ModelConfig config{};
    // config.model.modelName = "deepseek-flash";
    // config.model.protocol = ApiProtocol::OpenAICompatible;
    // config.model.provider = ProviderType::DeepSeek;
    // config.baseUrl = "https://api.deepseek.com";
    // config.apiKey = "sk-4cecc942640c4badbfae92b73c6eb1d5";
    // config.timeoutSeconds = 120;
    // ModelConfig config{};
    // config.model.modelName = "qwen3.8-flash";
    // config.model.protocol = ApiProtocol::OpenAICompatible;
    // config.model.provider = ProviderType::Bailian;
    // config.baseUrl = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    // config.apiKey = "sk-c02ef573727b4c049549c01bf7d523b2";
    // config.timeoutSeconds = 120;
    ModelConfig config{};
    config.model.modelName = "qwen3.8:27b-mlx";
    config.model.protocol = ApiProtocol::Ollama;
    config.model.provider = ProviderType::Ollama;
    config.baseUrl = "http://host.docker.internal:11434";
    config.timeoutSeconds = 300;

    if (!provider.initialize(config))
    {
        std::cerr << "初始化失败：" << provider.getLastError() << '\n';
        return 1;
    }

    provider.setSystemPrompt("你是一个精通C++的编程专家。");
    testConfig = config;
    return RUN_ALL_TESTS();
}

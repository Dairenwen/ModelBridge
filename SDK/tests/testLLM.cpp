#include "LLMProvider.hpp"
#include <gtest/gtest.h>
#include <json/json.h>
#include <iostream>
#include <string>

using namespace modelBridge;

namespace
{
    OpenAICompatibleProvider provider;
    ModelConfig testConfig; // 在 main 中保存完整配置，供不同思考模式的测试复用

    // 仅在测试中观察请求字段和思考内容长度，仍使用 SDK 原有的构造、解析和 HTTP 流程。
    class ThinkingTestProvider : public OpenAICompatibleProvider
    {
    public:
        mutable Json::Value lastRequest;
        mutable std::size_t thinkingBytes = 0;

    protected:
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream) const override
        {
            lastRequest = OpenAICompatibleProvider::makeRequestBody(messages, stream);
            return lastRequest;
        }

        std::string readResponse(const Json::Value &payload, bool stream, bool &finished) const override
        {
            auto reply = OpenAICompatibleProvider::readResponse(payload, stream, finished);
            const auto &thinking = payload["choices"][0][stream ? "delta" : "message"]["reasoning_content"];
            if (thinking.isString())
                thinkingBytes += thinking.asString().size();
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
        ASSERT_FALSE(firstReply.empty());
        const auto reply = provider.sendMessage(secondMessage);

        EXPECT_NE(reply.find(code), std::string::npos); // 当前消息未重复代号，回答需依赖历史。
        const auto &history = provider.getHistory();
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history[0].role, MessageRole::User);
        EXPECT_EQ(history[0].content, firstMessage);
        EXPECT_EQ(history[1].role, MessageRole::Assistant);
        EXPECT_EQ(history[1].content, firstReply);
        EXPECT_EQ(history[2].role, MessageRole::User);
        EXPECT_EQ(history[2].content, secondMessage);
        EXPECT_EQ(history[3].role, MessageRole::Assistant);
        EXPECT_EQ(history[3].content, reply);
        std::cout << "全量回复：" << reply << '\n';
    }

    TEST(LLMTest, StreamResponse)
    {
        provider.clearHistory();
        const auto code = "MB_STREAM_" + std::to_string(currentTimeMillis());
        const auto firstMessage = "请记住本轮对话的代号：" + code + "。只回答收到。";
        const std::string secondMessage = "上一条用户消息中的代号是什么？只返回该代号。";
        const auto firstReply = provider.sendMessageStream(firstMessage, [](const std::string &, bool) {});
        ASSERT_FALSE(firstReply.empty());
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

        EXPECT_NE(reply.find(code), std::string::npos);
        EXPECT_EQ(reply, received);
        EXPECT_EQ(doneCount, 1);
        const auto &history = provider.getHistory();
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history[0].role, MessageRole::User);
        EXPECT_EQ(history[0].content, firstMessage);
        EXPECT_EQ(history[1].role, MessageRole::Assistant);
        EXPECT_EQ(history[1].content, firstReply);
        EXPECT_EQ(history[2].role, MessageRole::User);
        EXPECT_EQ(history[2].content, secondMessage);
        EXPECT_EQ(history[3].role, MessageRole::Assistant);
        EXPECT_EQ(history[3].content, reply);
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
            EXPECT_NE(reply.find("7"), std::string::npos);
            EXPECT_EQ(thinkingProvider.lastRequest["thinking"]["type"].asString(), enabled ? "enabled" : "disabled");
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
            EXPECT_NE(streamReply.find("14"), std::string::npos);
            EXPECT_EQ(streamReply, received);
            EXPECT_EQ(doneCount, 1);
            EXPECT_FALSE(thinkingProvider.lastRequest.isMember("reasoning_effort"));
            EXPECT_EQ(thinkingProvider.thinkingBytes > 0, enabled);
            EXPECT_EQ(thinkingProvider.getHistory().size(), 4u);
            std::cout << "thinkingEnabled=" << enabled << "，流式思考字节数=" << thinkingProvider.thinkingBytes << '\n';
        }
    }

    TEST(LLMTest, ReasoningEffort)
    {
        // 固定开启思考，仅改变深度；不以思考字节数大小判断实际推理深浅。
        for (const std::string effort : {"low", "high"})
        {
            SCOPED_TRACE(effort);
            auto options = testConfig;
            options.thinkingEnabled = true;
            options.reasoningEffort = effort;

            ThinkingTestProvider thinkingProvider;
            ASSERT_TRUE(thinkingProvider.initialize(options)) << thinkingProvider.getLastError();
            thinkingProvider.setSystemPrompt("请简短回答。");
            const auto reply = thinkingProvider.sendMessage("7 加 8 等于多少？只回答数字。");
            EXPECT_NE(reply.find("15"), std::string::npos);
            EXPECT_EQ(thinkingProvider.lastRequest["thinking"]["type"].asString(), "enabled");
            EXPECT_EQ(thinkingProvider.lastRequest["reasoning_effort"].asString(), effort);

            std::string received;
            int doneCount = 0;
            const auto streamReply = thinkingProvider.sendMessageStream(
                "将上一轮的计算结果加 1，只回答数字。", [&](const std::string &delta, bool done)
                {
                    received += delta;
                    if (done)
                        ++doneCount; });
            EXPECT_NE(streamReply.find("16"), std::string::npos);
            EXPECT_EQ(streamReply, received);
            EXPECT_EQ(doneCount, 1);
            EXPECT_EQ(thinkingProvider.lastRequest["reasoning_effort"].asString(), effort);
            EXPECT_EQ(thinkingProvider.getHistory().size(), 4u);
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
    ModelConfig config{};
    config.model.modelName = "qwen3.8-flash";
    config.model.protocol = ApiProtocol::OpenAICompatible;
    config.model.provider = ProviderType::Bailian;
    config.baseUrl = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    config.apiKey = "sk-c02ef573727b4c049549c01bf7d523b2";
    config.timeoutSeconds = 120;

    if (!provider.initialize(config))
    {
        std::cerr << "初始化失败：" << provider.getLastError() << '\n';
        return 1;
    }

    provider.setSystemPrompt("你是一个精通C++的编程专家。");
    testConfig = config;
    return RUN_ALL_TESTS();
}

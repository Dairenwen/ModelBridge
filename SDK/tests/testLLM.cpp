#include "LLMProvider.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace modelBridge;

namespace
{
    ModelConfig makeOllamaConfig(const std::string &modelName = "test-model")
    {
        ModelConfig config{};
        config.model.modelName = modelName;
        config.model.protocol = ApiProtocol::Ollama;
        config.model.provider = ProviderType::Ollama;
        config.baseUrl = "http://127.0.0.1:11434";
        config.timeoutSeconds = 120;
        // 仅测试本地 Ollama，不设置 apiKey 或 apiKeyEnv。
        return config;
    }

    ModelConfig makeCompatibleConfig()
    {
        auto config = makeOllamaConfig();
        config.model.protocol = ApiProtocol::OpenAICompatible;
        config.model.provider = ProviderType::OpenAI;
        config.baseUrl = "http://127.0.0.1:8000/v1";
        return config;
    }

    TEST(LLMInitialization, LocalOllamaDoesNotRequireApiKey)
    {
        OllamaProvider provider;
        const auto config = makeOllamaConfig();

        EXPECT_FALSE(provider.isInitialized());
        ASSERT_TRUE(provider.initialize(config)) << provider.getLastError();
        EXPECT_TRUE(provider.isInitialized());
        EXPECT_TRUE(provider.getLastError().empty());
        EXPECT_EQ(provider.getModelName(), config.model.modelName);
        EXPECT_EQ(provider.getProtocol(), ApiProtocol::Ollama);
    }

    TEST(LLMInitialization, CompatibleProviderAcceptsConfiguredApiKey)
    {
        OpenAICompatibleProvider provider;
        auto config = makeCompatibleConfig();
        config.apiKey = "test-key"; // 只校验初始化，不发送请求。

        ASSERT_TRUE(provider.initialize(config)) << provider.getLastError();
        EXPECT_TRUE(provider.isInitialized());
        EXPECT_TRUE(provider.getLastError().empty());
    }

    TEST(LLMInitialization, ChangingApiKeyKeepsInitializationState)
    {
        OpenAICompatibleProvider provider;
        auto config = makeCompatibleConfig();
        config.apiKey = "first-test-key";
        ASSERT_TRUE(provider.initialize(config)) << provider.getLastError();

        // setApiKey 只更新密钥，不重新初始化对象。
        provider.setApiKey("second-test-key");
        EXPECT_EQ(provider.getApiKey(), "second-test-key");
        EXPECT_TRUE(provider.isInitialized());

        provider.setApiKey("");
        EXPECT_TRUE(provider.getApiKey().empty());
        EXPECT_TRUE(provider.isInitialized());
    }

    TEST(LLMInitialization, SettingApiKeyDoesNotBypassInvalidConfiguration)
    {
        OpenAICompatibleProvider provider;
        auto config = makeCompatibleConfig();
        config.model.modelName.clear();

        ASSERT_FALSE(provider.initialize(config));
        provider.setApiKey("test-key");
        EXPECT_FALSE(provider.isInitialized());
        EXPECT_FALSE(provider.getLastError().empty());
    }

    TEST(LLMInitialization, RejectsUrlWithoutHost)
    {
        OllamaProvider provider;
        auto config = makeOllamaConfig();
        config.baseUrl = "http://";

        EXPECT_FALSE(provider.initialize(config));
        EXPECT_FALSE(provider.isInitialized());
        EXPECT_FALSE(provider.getLastError().empty());
    }

    class OllamaChatTest : public testing::Test
    {
    protected:
        void SetUp() override
        {
            const char *modelName = std::getenv("OLLAMA_MODEL");
            if (modelName == nullptr || *modelName == '\0')
                GTEST_SKIP() << "设置 OLLAMA_MODEL 为本地已安装的模型名后，再运行对话测试";

            ASSERT_TRUE(provider.initialize(makeOllamaConfig(modelName)))
                << provider.getLastError();
        }

        OllamaProvider provider;
    };

    TEST_F(OllamaChatTest, FullResponseUsesConversationHistory)
    {
        // 每个测试自行维护历史，Provider 不保存对话。
        std::vector<Message> history{
            {MessageRole::System, "请简洁回答，并准确保留用户给出的代号。", 0, {}},
            {MessageRole::User, "我的项目代号是 MB-4729。请记住这个代号。", 0, {}}};

        const auto firstReply = provider.sendMessage(history);
        ASSERT_FALSE(firstReply.empty());
        history.push_back({MessageRole::Assistant, firstReply, 0, {}});
        history.push_back({MessageRole::User, "我刚才的项目代号是什么？请只回复代号。", 0, {}});

        // 第二轮必须携带 System、首轮 User、真实 Assistant 回复及新问题。
        const auto reply = provider.sendMessage(history);
        ASSERT_FALSE(reply.empty());
        history.push_back({MessageRole::Assistant, reply, 0, {}});
        // 输出用于观察上下文回答，不把模型是否照抄代号作为 SDK 的断言。
        std::cout << "全量回复：" << reply << '\n';
    }

    TEST_F(OllamaChatTest, StreamResponseUsesConversationHistory)
    {
        std::vector<Message> history{
            {MessageRole::System, "请简洁回答，并准确保留用户给出的代号。", 0, {}},
            {MessageRole::User, "我的项目代号是 MB-8351。请记住这个代号。", 0, {}}};

        // 两轮都走流式接口，避免流式测试依赖全量接口。
        const auto firstReply = provider.sendMessageStream(
            history, [](const std::string &, bool) {});
        ASSERT_FALSE(firstReply.empty());
        history.push_back({MessageRole::Assistant, firstReply, 0, {}});
        history.push_back({MessageRole::User, "我刚才的项目代号是什么？请只回复代号。", 0, {}});

        std::string received;
        int doneCount = 0;
        int textChunkCount = 0;
        bool completed = false;
        std::cout << "流式回复：" << std::flush;

        const auto reply = provider.sendMessageStream(
            history, [&](const std::string &delta, bool done)
            {
                EXPECT_FALSE(completed) << "结束通知后不应继续回调";
                received += delta;
                if (!delta.empty())
                {
                    ++textChunkCount;
                    std::cout << delta << std::flush;
                }
                if (done)
                {
                    EXPECT_TRUE(delta.empty()) << "约定的结束通知应为 (空文本, true)";
                    completed = true;
                    ++doneCount;
                }
            });
        std::cout << '\n';

        ASSERT_FALSE(reply.empty());
        EXPECT_EQ(reply, received);
        EXPECT_GT(textChunkCount, 0); // 不假定网络必须把回复切成多个片段。
        EXPECT_TRUE(completed);
        EXPECT_EQ(doneCount, 1);

        // 流式片段用于展示，只把最终完整回复追加到历史一次。
        history.push_back({MessageRole::Assistant, reply, 0, {}});
    }
} // namespace

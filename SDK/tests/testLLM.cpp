#include "LLMProvider.hpp"
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

using namespace modelBridge;

namespace
{
    OpenAICompatibleProvider provider;

    // 调用方自行构造历史；每项测试使用独立副本。
    std::vector<Message> makeHistory()
    {
        return {
            {MessageRole::System, "请用简洁的中文回答。", 0, {}, {}},
            {MessageRole::User, "我正在学习 C++。", 0, {}, {}},
            {MessageRole::Assistant, "好的，我会使用 C++ 举例。", 0, {}, {}},
            {MessageRole::User, "请用一句话解释智能指针。", 0, {}, {}}};
    }

    TEST(LLMTest, Initialization)
    {
        EXPECT_TRUE(provider.isInitialized());
    }

    TEST(LLMTest, FullResponse)
    {
        auto history = makeHistory();
        const auto reply = provider.sendMessage(history);

        EXPECT_FALSE(reply.empty());
        history.push_back({MessageRole::Assistant, reply, 0, {}, {}});
        std::cout << "全量回复：" << reply << '\n';
    }

    TEST(LLMTest, StreamResponse)
    {
        auto history = makeHistory();
        std::string received;
        int doneCount = 0;
        std::cout << "流式回复：" << std::flush;

        const auto reply = provider.sendMessageStream(
            history, [&](const std::string &delta, bool done)
            {
                EXPECT_EQ(doneCount, 0) << "结束后不应继续回调";
                received += delta;
                std::cout << delta << std::flush;
                if (done)
                    ++doneCount; });
        std::cout << '\n';

        EXPECT_FALSE(reply.empty());
        EXPECT_EQ(reply, received);
        EXPECT_EQ(doneCount, 1);
        // 流式片段只用于展示，完整回复只保存一次。
        history.push_back({MessageRole::Assistant, reply, 0, {}, {}});
    }
} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    // 运行前填写实际模型及服务根地址，并设置 OPENAI_API_KEY 环境变量。
    ModelConfig config{};
    config.model.modelName = "";
    config.model.protocol = ApiProtocol::OpenAICompatible;
    config.model.provider = ProviderType::Custom;
    config.baseUrl = "http://127.0.0.1:8000/v1";
    config.apiKeyEnv = "OPENAI_API_KEY";
    config.timeoutSeconds = 120;

    if (!provider.initialize(config))
    {
        std::cerr << "初始化失败：" << provider.getLastError() << '\n';
        return 1;
    }

    return RUN_ALL_TESTS();
}

#include "LLMProvider.hpp"
#include <gtest/gtest.h>
#include <json/json.h>
#include <iostream>
#include <string>

using namespace modelBridge;

namespace
{
    // 测试固定使用 Responses，请求构造及解析仍使用 SDK 实现。
    class PackyCodeProvider : public OpenAICompatibleProvider
    {
    public:
        mutable std::string lastRequestPath;

    protected:
        void resetProtocol() override
        {
            OpenAICompatibleProvider::resetProtocol();
            // 仅用于测试准备：借用已有切换入口选择 Responses，不发送 Chat 请求。
            Json::Value error;
            error["error"]["code"] = "protocol_not_supported";
            OpenAICompatibleProvider::tryProtocolFallback(error);
        }

        std::string makeRequestPath(bool stream) const override
        {
            lastRequestPath = OpenAICompatibleProvider::makeRequestPath(stream);
            return lastRequestPath;
        }
    };

    PackyCodeProvider provider;

    TEST(PackyCodeTest, Initialization)
    {
        EXPECT_TRUE(provider.isInitialized());
    }

    TEST(PackyCodeTest, FullResponse)
    {
        provider.clearHistory();
        const auto result = provider.sendMessage("你好，请用一句话介绍你自己。");

        EXPECT_EQ(provider.lastRequestPath, "/responses");
        EXPECT_FALSE(result.content.empty());
        std::cout << "请求路径：" << provider.lastRequestPath
                  << "\n全量回复：" << result.content << '\n';
    }

    TEST(PackyCodeTest, StreamResponse)
    {
        provider.clearHistory();
        std::string received;
        int doneCount = 0;
        std::cout << "流式回复：" << std::flush;
        const auto result = provider.sendMessageStream(
            "你好，请用一句话介绍你自己。", [&](const std::string &delta, bool done)
            {
                EXPECT_EQ(doneCount, 0); // 完成通知之后不应再收到增量。
                received += delta;
                std::cout << delta << std::flush;
                if (done)
                    ++doneCount; });

        EXPECT_EQ(provider.lastRequestPath, "/responses");
        EXPECT_FALSE(received.empty());
        EXPECT_EQ(result.content, received);
        EXPECT_EQ(doneCount, 1);
        std::cout << "\n请求路径：" << provider.lastRequestPath << '\n';
    }
} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    ModelConfig config{};
    config.model.modelName = "gpt-6.1-sol";
    config.model.protocol = ApiProtocol::OpenAICompatible;
    config.model.provider = ProviderType::Custom;
    config.baseUrl = "https://rightapi.ai/v1";
    config.apiKey = "sk-838023b362414f1e82f69bcd4aa183ea";
    config.apiKeyEnv = "PACKY_API_KEY";
    config.timeoutSeconds = 120;

    if (!provider.initialize(config))
    {
        std::cerr << "初始化失败：" << provider.getLastError() << '\n';
        return 1;
    }

    return RUN_ALL_TESTS();
}

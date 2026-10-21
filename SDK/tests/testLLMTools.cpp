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

    TEST(ToolCallTest, FullAndStream)
    {
        // 依次测试非流式调用和流式调用
        for (const bool stream : {false, true})
        {
            SCOPED_TRACE(stream ? "stream tools" : "full tools");
            auto &toolProvider = provider;
            toolProvider.clearHistory(); // 全量和流式测试分别开始新对话。
            const auto token = "MB_TOOL_" + std::to_string(currentTimeMillis());
            int executions = 0;
            toolProvider.setTools({// 1. 获取测试代号
                                   {
                                       "get_test_token",
                                       "获取本轮随机测试代号，必须调用工具才能知道代号。",
                                       R"({"type":"object","properties":{},"additionalProperties":false})",
                                       [&](const std::string &)
                                       {
                                           ++executions;
                                           return token;
                                       }},

                                   // 2. 加法工具
                                   {
                                       "add",
                                       "计算两个数字的和，返回加法结果。",
                                       R"({"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false})",
                                       [](const std::string &arguments)
                                       {
                                           Json::Value args;
                                           std::istringstream input(arguments);
                                           input >> args;
                                           const double a = args["a"].asDouble();
                                           const double b = args["b"].asDouble();
                                           return std::to_string(a + b);
                                       }},

                                   // 3. 乘法工具
                                   {
                                       "multiply",
                                       "计算两个数字的乘积，返回乘法结果。",
                                       R"({"type":"object","properties":{"a":{"type":"number"},"b":{"type":"number"}},"required":["a","b"],"additionalProperties":false})",
                                       [](const std::string &arguments)
                                       {
                                           Json::Value args;
                                           std::istringstream input(arguments);
                                           input >> args;
                                           const double a = args["a"].asDouble();
                                           const double b = args["b"].asDouble();
                                           return std::to_string(a * b);
                                       }}});

            const std::string prompt =
                "请使用工具计算 (12 + 8) × 3，"
                "必须先调用 add，再使用 add 的返回结果调用 multiply，"
                "最后返回计算结果。";

            std::string received;
            int doneCount = 0;
            const auto result = stream ? toolProvider.sendMessageStream(prompt, [&](const std::string &delta, bool done)
                                                                        {
                                                                            received += delta;
                                                                            if (done)
                                                                                ++doneCount; })
                                       : toolProvider.sendMessage(prompt);

            // 按实际消息顺序检查两次工具执行，不固定 Assistant 消息的位置。
            std::vector<const Message *> outputs;
            for (const auto &message : result.messages)
            {
                if (message.role == MessageRole::Tool)
                {
                    outputs.push_back(&message);
                    std::cout << "工具调用：" << message.toolName << "，结果：" << message.content << '\n';
                }
            }
            ASSERT_EQ(outputs.size(), 2u);
            EXPECT_EQ(outputs[0]->toolName, "add");
            EXPECT_DOUBLE_EQ(std::stod(outputs[0]->content), 20.0);
            EXPECT_EQ(outputs[1]->toolName, "multiply");
            EXPECT_DOUBLE_EQ(std::stod(outputs[1]->content), 60.0);
            EXPECT_NE(result.content.find("60"), std::string::npos);
            EXPECT_EQ(toolProvider.getHistory().size(), result.messages.size());
            if (stream)
            {
                EXPECT_EQ(received, result.content);
                EXPECT_EQ(doneCount, 1);
            }

            // 后续发送仍由 SDK 回放工具请求和结果，调用方无需重建历史。
            toolProvider.setTools({});
            EXPECT_NE(toolProvider.sendMessage("上一轮的最终计算结果是什么？只回答数字。").content.find("60"), std::string::npos);
            EXPECT_EQ(executions, 0); // 本题不应调用获取代号的工具。
        }
    }

} // namespace

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    // ModelConfig config{};
    // config.model.modelName = "deepseek-flash";
    // config.model.protocol = ApiProtocol::OpenAICompatible;
    // config.model.provider = ProviderType::DeepSeek;
    // config.baseUrl = "https://api.deepseek.com";
    // config.apiKey = "sk-4cecc942640c4badbfae92b73c6eb1d5";
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

    return RUN_ALL_TESTS();
}

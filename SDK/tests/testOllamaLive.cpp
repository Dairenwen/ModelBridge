#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include "Util.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <cstdlib>
#include <future>
#include <unordered_set>

using namespace modelBridge;

namespace {
// Explicit opt-in: no model requests occur in the default test run.
// MODELBRIDGE_LIVE_OLLAMA_URL=http://127.0.0.1:11434
// MODELBRIDGE_LIVE_OLLAMA_MODEL=qwen3.8:27b-mlx
// MODELBRIDGE_LIVE_OLLAMA_ALT_MODEL=qwen2.5:7b-instruct-q4_K_M
class OllamaLive : public testing::Test {
protected:
    ModelConfig config;
    std::shared_ptr<SessionManager> sessions = std::make_shared<SessionManager>();

    void SetUp() override {
        const auto* url = std::getenv("MODELBRIDGE_LIVE_OLLAMA_URL");
        const auto* model = std::getenv("MODELBRIDGE_LIVE_OLLAMA_MODEL");
        if (!url || !model)
            GTEST_SKIP() << "Set MODELBRIDGE_LIVE_OLLAMA_URL and MODELBRIDGE_LIVE_OLLAMA_MODEL to run real requests";
        config.configId = "live-original";
        config.model.protocol = ApiProtocol::Ollama;
        config.model.provider = ProviderType::Ollama;
        config.model.modelName = model;
        config.baseUrl = url;
        config.temperature = 0;
        config.maxOutputTokens = 512;
        config.timeoutSeconds = 120;
        config.thinkingEnabled = false;
    }

    std::string session() {
        return sessions->createSession("live-test", config.configId, "live-test",
            "Follow the user's instructions precisely. Keep answers brief. Copy secret codes exactly.");
    }
    std::string token() { return "MB_" + std::to_string(currentTimeMillis()); }

    void expectStored(const std::string& id, const ChatResult& result) {
        auto saved = sessions->getMessages(id).value();
        std::unordered_set<std::string> ids;
        for (const auto& message : saved) {
            EXPECT_FALSE(message.messageId.empty());
            EXPECT_TRUE(ids.insert(message.messageId).second);
        }
        for (const auto& message : result.messages) {
            EXPECT_EQ(ids.count(message.messageId), 1u);
            EXPECT_GT(message.createdAt, 0);
            EXPECT_FALSE(message.modelName.empty());
            auto stored = std::find_if(saved.begin(), saved.end(), [&](const Message& entry) {
                return entry.messageId == message.messageId;
            });
            ASSERT_NE(stored, saved.end());
            EXPECT_EQ(stored->role, message.role);
            EXPECT_EQ(stored->content, message.content);
            EXPECT_EQ(stored->createdAt, message.createdAt);
            EXPECT_EQ(stored->modelConfigId, message.modelConfigId);
            EXPECT_EQ(stored->modelName, message.modelName);
            EXPECT_EQ(stored->reasoningEffort, message.reasoningEffort);
            EXPECT_EQ(stored->thinkingEnabled, message.thinkingEnabled);
            EXPECT_EQ(stored->reasoningContent, message.reasoningContent);
            EXPECT_EQ(stored->protocolData, message.protocolData);
            EXPECT_EQ(stored->toolCallId, message.toolCallId);
            EXPECT_EQ(stored->toolName, message.toolName);
            ASSERT_EQ(stored->toolCalls.size(), message.toolCalls.size());
            for (std::size_t i = 0; i < message.toolCalls.size(); ++i) {
                EXPECT_EQ(stored->toolCalls[i].id, message.toolCalls[i].id);
                EXPECT_EQ(stored->toolCalls[i].name, message.toolCalls[i].name);
                EXPECT_EQ(stored->toolCalls[i].arguments, message.toolCalls[i].arguments);
            }
        }
    }
};

TEST_F(OllamaLive, FullAndStreamHistory) {
    OllamaProvider provider;
    ASSERT_TRUE(provider.initialize(config));
    auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
    const auto code = token();
    auto first = provider.sendMessage("Remember this secret code: " + code + ". Reply only OK.");
    ASSERT_FALSE(first.content.empty());
    std::string streamed; int done = 0;
    auto next = provider.sendMessageStream("What was the secret code? Reply with the code only.",
        [&](const std::string& delta, bool finished) {
            EXPECT_EQ(done, 0); streamed += delta;
            if (finished) ++done;
            provider.getHistory(); // Re-entry must not deadlock.
        });
    EXPECT_NE(next.content.find(code), std::string::npos) << next.content;
    EXPECT_EQ(streamed, next.content);
    EXPECT_EQ(done, 1);
    EXPECT_EQ(provider.getHistory().size(), 4u);
    EXPECT_EQ(sessions->getMessages(id)->size(), 5u);
    expectStored(id, first); expectStored(id, next);
    EXPECT_TRUE(sessions->addMessages(id, next.messages, config));
    EXPECT_EQ(sessions->getMessages(id)->size(), 5u);
}

TEST_F(OllamaLive, SwitchActualModelsAndRestoreInAnotherProvider) {
    const auto* other = std::getenv("MODELBRIDGE_LIVE_OLLAMA_ALT_MODEL");
    if (!other) GTEST_SKIP() << "Set MODELBRIDGE_LIVE_OLLAMA_ALT_MODEL for a real model switch";
    OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
    auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
    const auto code = token();
    auto first = provider.sendMessage("Remember this secret code: " + code + ". Reply only OK.");
    auto alternate = config;
    alternate.model.modelName = other;
    alternate.configId = "live-alternate";
    alternate.thinkingEnabled.reset();
    ASSERT_TRUE(provider.initialize(alternate));
    auto second = provider.sendMessage("What was the secret code? Reply with the code only.");
    EXPECT_NE(second.content.find(code), std::string::npos) << second.content;
    EXPECT_EQ(second.messages.front().modelName, other);
    EXPECT_EQ(second.messages.front().modelConfigId, alternate.configId);
    EXPECT_EQ(first.messages.front().modelName, config.model.modelName);
    OllamaProvider restored;
    ASSERT_TRUE(restored.initialize(config)); ASSERT_TRUE(restored.bindSession(sessions, id));
    auto third = restored.sendMessage("Repeat the secret code from this conversation. Reply with the code only.");
    EXPECT_NE(third.content.find(code), std::string::npos) << third.content;
    EXPECT_EQ(sessions->getMessages(id)->size(), 7u);
    expectStored(id, first); expectStored(id, second); expectStored(id, third);
}

TEST_F(OllamaLive, FullAndStreamToolCallsThenHistoryReplay) {
    for (bool stream : {false, true}) {
        SCOPED_TRACE(stream ? "stream" : "full");
        OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
        auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
        const auto code = token(); int executions = 0;
        provider.setTools({{"get_secret_code", "Read the secret code. It is available only through this tool.",
            R"({"type":"object","properties":{},"required":[]})",
            [&](const std::string&) { ++executions; return code; }}});
        std::string text; int done = 0;
        const std::string prompt = "Call get_secret_code to read the secret code, then output the exact code. Do not invent a code.";
        auto result = stream ? provider.sendMessageStream(prompt, [&](const std::string& delta, bool finished) {
            text += delta; if (finished) ++done;
        }) : provider.sendMessage(prompt);
        EXPECT_GE(executions, 1);
        EXPECT_NE(result.content.find(code), std::string::npos) << result.content;
        std::size_t calls = 0, outputs = 0;
        for (const auto& message : result.messages) {
            calls += message.toolCalls.size();
            if (message.role == MessageRole::Tool) {
                ++outputs;
                EXPECT_EQ(message.toolName, "get_secret_code");
                EXPECT_EQ(message.content, code);
            }
        }
        EXPECT_EQ(calls, outputs); EXPECT_EQ(outputs, static_cast<std::size_t>(executions));
        if (stream) { EXPECT_EQ(text, result.content); EXPECT_EQ(done, 1); }
        expectStored(id, result);
        provider.setTools({});
        auto recalled = provider.sendMessage("What code did the tool return previously? Reply with that exact code only.");
        EXPECT_NE(recalled.content.find(code), std::string::npos) << recalled.content;
        expectStored(id, recalled);
    }
}

TEST_F(OllamaLive, ConcurrentRequestsKeepWholeTurns) {
    OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
    auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
    auto a = std::async(std::launch::async, [&] { return provider.sendMessage("Reply exactly ALPHA."); });
    auto b = std::async(std::launch::async, [&] { return provider.sendMessage("Reply exactly BETA."); });
    auto first = a.get(), second = b.get();
    EXPECT_NE(first.content.find("ALPHA"), std::string::npos) << first.content;
    EXPECT_NE(second.content.find("BETA"), std::string::npos) << second.content;
    auto saved = provider.getHistory(); ASSERT_EQ(saved.size(), 4u);
    EXPECT_EQ(saved[0].role, MessageRole::User); EXPECT_EQ(saved[1].role, MessageRole::Assistant);
    EXPECT_EQ(saved[2].role, MessageRole::User); EXPECT_EQ(saved[3].role, MessageRole::Assistant);
    expectStored(id, first); expectStored(id, second);
}

TEST_F(OllamaLive, SessionSwitchDuringStreamSavesOriginalSession) {
    OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
    auto old = session(), next = session(); ASSERT_TRUE(provider.bindSession(sessions, old));
    bool switched = false;
    auto result = provider.sendMessageStream("Reply with the numbers one through ten in English.",
        [&](const std::string& delta, bool) {
            if (!switched && !delta.empty()) {
                switched = true; EXPECT_TRUE(provider.bindSession(sessions, next));
            }
        });
    ASSERT_TRUE(switched); EXPECT_FALSE(result.content.empty());
    EXPECT_EQ(sessions->getMessages(old)->size(), 3u);
    EXPECT_EQ(sessions->getMessages(next)->size(), 1u);
    EXPECT_TRUE(provider.getHistory().empty());
    expectStored(old, result);
}

TEST_F(OllamaLive, ClearAndDeleteDuringStreamRejectStaleResults) {
    for (bool remove : {false, true}) {
        OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
        auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
        bool changed = false;
        auto result = provider.sendMessageStream("Reply with the numbers one through ten in English.",
            [&](const std::string& delta, bool) {
                if (!changed && !delta.empty()) {
                    changed = true;
                    if (remove) EXPECT_TRUE(sessions->deleteSession(id));
                    else provider.clearHistory();
                }
            });
        ASSERT_TRUE(changed); EXPECT_FALSE(result.content.empty());
        if (remove) EXPECT_FALSE(sessions->getSession(id));
        else EXPECT_EQ(sessions->getMessages(id)->size(), 1u);
    }
}

TEST_F(OllamaLive, ToolHistoryAcrossOllamaAndOpenAICompatibleInterfaces) {
    OllamaProvider original; ASSERT_TRUE(original.initialize(config));
    auto id = session(); ASSERT_TRUE(original.bindSession(sessions, id));
    const auto code = token(); int executions = 0;
    original.setTools({{"get_secret_code", "Read the secret code.",
        R"({"type":"object","properties":{},"required":[]})",
        [&](const std::string&) { ++executions; return code; }}});
    auto first = original.sendMessage("Call get_secret_code and return its exact result.");
    ASSERT_GE(executions, 1);
    EXPECT_NE(first.content.find(code), std::string::npos) << first.content;
    auto compatible = config;
    compatible.configId = "live-openai-compatible";
    compatible.model.protocol = ApiProtocol::OpenAICompatible;
    compatible.model.provider = ProviderType::Custom;
    compatible.baseUrl += "/v1";
    compatible.thinkingEnabled.reset();
    compatible.maxOutputTokens = 1024;
    compatible.apiKey = "local-test-placeholder"; // Local Ollama needs no real credential.
    OpenAICompatibleProvider next; ASSERT_TRUE(next.initialize(compatible));
    ASSERT_TRUE(next.bindSession(sessions, id));
    auto recalled = next.sendMessage("What exact secret code did the tool return? Reply with that code only.");
    EXPECT_NE(recalled.content.find(code), std::string::npos) << recalled.content;
    original.setTools({});
    auto back = original.sendMessage("Repeat the same secret code, without explanations.");
    EXPECT_NE(back.content.find(code), std::string::npos) << back.content;
    expectStored(id, first); expectStored(id, recalled); expectStored(id, back);
}

TEST_F(OllamaLive, SelectExistingSessionIdsResumesTheirOwnConversations) {
    auto a = session(), b = session();
    const auto codeA = token() + "_A", codeB = token() + "_B";
    {
        OllamaProvider original; ASSERT_TRUE(original.initialize(config));
        ASSERT_TRUE(original.bindSession(sessions, a));
        auto first = original.sendMessage("Remember this secret code: " + codeA + ". Reply only OK.");
        expectStored(a, first);
        ASSERT_TRUE(original.bindSession(sessions, b));
        auto second = original.sendMessage("Remember this secret code: " + codeB + ". Reply only OK.");
        expectStored(b, second);
    }
    OllamaProvider restored; ASSERT_TRUE(restored.initialize(config));
    ASSERT_TRUE(restored.bindSession(sessions, a));
    auto first = restored.sendMessage("What was the secret code? Reply with that code only.");
    EXPECT_NE(first.content.find(codeA), std::string::npos) << first.content;
    EXPECT_EQ(first.content.find(codeB), std::string::npos);
    ASSERT_TRUE(restored.bindSession(sessions, b));
    EXPECT_FALSE(restored.bindSession(sessions, "nonexistent"));
    auto second = restored.sendMessage("What was the secret code? Reply with that code only.");
    EXPECT_NE(second.content.find(codeB), std::string::npos) << second.content;
    EXPECT_EQ(second.content.find(codeA), std::string::npos);
    EXPECT_EQ(sessions->getMessages(a)->size(), 5u);
    EXPECT_EQ(sessions->getMessages(b)->size(), 5u);
    expectStored(a, first); expectStored(b, second);
}

TEST_F(OllamaLive, RestoredSessionExposesToolArgumentsResultsAndMetadata) {
    auto id = session();
    const auto code = token();
    ChatResult result;
    int executions = 0;
    {
        OllamaProvider original; ASSERT_TRUE(original.initialize(config));
        ASSERT_TRUE(original.bindSession(sessions, id));
        original.setTools({{"read_code", "Return the secret code for the requested label.",
            R"({"type":"object","properties":{"label":{"type":"string","enum":["selected"]}},"required":["label"]})",
            [&](const std::string& args) {
                ++executions;
                EXPECT_EQ(parseJson(args)["label"].asString(), "selected");
                return code;
            }}});
        result = original.sendMessage("Call read_code with label selected and return the exact secret code.");
        ASSERT_GE(executions, 1);
        EXPECT_NE(result.content.find(code), std::string::npos) << result.content;
    }
    auto saved = sessions->getSession(id).value();
    std::size_t calls = 0, outputs = 0;
    for (const auto& message : saved.messages) {
        for (const auto& call : message.toolCalls) {
            ++calls;
            EXPECT_EQ(call.name, "read_code");
            EXPECT_EQ(parseJson(call.arguments)["label"].asString(), "selected");
        }
        if (message.role == MessageRole::Tool) {
            ++outputs;
            EXPECT_EQ(message.toolName, "read_code");
            EXPECT_EQ(message.content, code);
        }
    }
    EXPECT_EQ(calls, static_cast<std::size_t>(executions)); EXPECT_EQ(outputs, calls);
    expectStored(id, result);
    OllamaProvider restored; ASSERT_TRUE(restored.initialize(config));
    ASSERT_TRUE(restored.bindSession(sessions, id));
    auto reply = restored.sendMessage("What exact code did the tool return previously? Reply with the code only.");
    EXPECT_NE(reply.content.find(code), std::string::npos) << reply.content;
    expectStored(id, result); expectStored(id, reply);
}

TEST_F(OllamaLive, ThinkingModeMetadata) {
    config.thinkingEnabled = true;
    config.maxOutputTokens = 1024;
    OllamaProvider provider; ASSERT_TRUE(provider.initialize(config));
    auto id = session(); ASSERT_TRUE(provider.bindSession(sessions, id));
    auto result = provider.sendMessage("Compute 17 * 23. Give the final number only.");
    EXPECT_NE(result.content.find("391"), std::string::npos) << result.content;
    bool hasThinking = false;
    for (const auto& message : result.messages) {
        EXPECT_EQ(message.thinkingEnabled, true);
        if (!message.reasoningContent.empty()) hasThinking = true;
    }
    EXPECT_TRUE(hasThinking);
    expectStored(id, result);
}
} // namespace

#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include "Util.hpp"
#include <gtest/gtest.h>
#include <atomic>
#include <cstdlib>
#include <future>
#include <thread>

using namespace modelBridge;
namespace {
ModelConfig config(ApiProtocol protocol = ApiProtocol::Ollama) {
    ModelConfig c{};
    c.configId = "original";
    c.model.modelName = "test-model";
    c.model.protocol = protocol;
    c.model.provider = protocol == ApiProtocol::Ollama ? ProviderType::Ollama : ProviderType::Custom;
    c.baseUrl = "http://127.0.0.1:14987/models";
    c.timeoutSeconds = 5;
    return c;
}
template<class Base> class Inspect : public Base {
public:
    Json::Value body(const std::vector<Message>& messages, bool responses = false) {
        typename Base::RequestContext context;
        context.config = config(this->getProtocol());
        context.responses = responses;
        return this->makeRequestBody(messages, false, context);
    }
};
// Hold the first request after snapshot creation. Subsequent requests must remain runnable.
class PausedProvider : public OllamaProvider {
public:
    std::promise<void> entered, release;
    std::shared_future<void> resume = release.get_future().share();
    mutable std::atomic<bool> first{true};
protected:
    Json::Value makeRequestBody(const std::vector<Message>& messages, bool stream, const RequestContext& context) const override {
        if (first.exchange(false)) {
            const_cast<PausedProvider*>(this)->entered.set_value();
            if (resume.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
                throw std::runtime_error("Test barrier timed out");
        }
        return OllamaProvider::makeRequestBody(messages, stream, context);
    }
};
class HttpHistory : public testing::Test {
protected:
    void SetUp() override {
        if (!std::getenv("MODELBRIDGE_RUN_LOCAL_HTTP"))
            GTEST_SKIP() << "Start fixtures/history.sim.yaml and set MODELBRIDGE_RUN_LOCAL_HTTP=1";
    }
};
// Exercise the real history lookup and request construction, stopping before HTTP.
class CapturedRequest : public OllamaProvider {
public:
    mutable Json::Value request;
protected:
    Json::Value makeRequestBody(const std::vector<Message>& messages, bool stream, const RequestContext& context) const override {
        request = OllamaProvider::makeRequestBody(messages, stream, context);
        throw std::runtime_error("Request captured before HTTP");
    }
};
TEST(SessionSafety, SelectSessionIdReplaysOnlyItsOwnCompleteHistory) {
    auto manager = std::make_shared<SessionManager>();
    auto c = config(); c.reasoningEffort = "low";
    auto first = manager->createSession("user", c.configId, "first", "first-system");
    auto second = manager->createSession("user", c.configId, "second", "second-system");
    Message user; user.content = "first-question";
    Message call; call.role = MessageRole::Assistant; call.content = "first-answer";
    call.toolCalls = {{"call-one", "lookup", R"({"item":"example"})"}};
    call.reasoningContent = "saved-thinking";
    call.protocolData = R"({"parts":[{"thoughtSignature":"saved-signature"}]})";
    Message output; output.role = MessageRole::Tool; output.toolCallId = "call-one";
    output.toolName = "lookup"; output.content = "saved-tool-result";
    ASSERT_TRUE(manager->addMessages(first, {user, call, output}, c));
    user.content = "second-question";
    ASSERT_TRUE(manager->addMessage(second, user, c));
    auto saved = manager->getSession(first).value();
    ASSERT_EQ(saved.messages.size(), 4u);
    const auto& storedCall = saved.messages[2];
    EXPECT_EQ(storedCall.modelConfigId, c.configId);
    EXPECT_EQ(storedCall.modelName, c.model.modelName);
    EXPECT_EQ(storedCall.reasoningEffort, c.reasoningEffort);
    EXPECT_EQ(storedCall.protocolData, call.protocolData);
    EXPECT_EQ(storedCall.reasoningContent, call.reasoningContent);
    ASSERT_EQ(storedCall.toolCalls.size(), 1u);
    EXPECT_EQ(storedCall.toolCalls[0].arguments, call.toolCalls[0].arguments);
    EXPECT_GT(storedCall.createdAt, 0);
    EXPECT_FALSE(storedCall.messageId.empty());
    auto messages = manager->getMessages(first).value();
    EXPECT_EQ(messages[3].toolCallId, "call-one");
    EXPECT_EQ(messages[3].toolName, "lookup");
    EXPECT_EQ(messages[3].content, "saved-tool-result");
    CapturedRequest provider; ASSERT_TRUE(provider.initialize(c));
    ASSERT_TRUE(provider.bindSession(manager, first));
    EXPECT_THROW(provider.sendMessage("first-followup"), std::runtime_error);
    auto body = provider.request["messages"];
    ASSERT_EQ(body.size(), 5u);
    EXPECT_EQ(body[0]["content"], "first-system");
    EXPECT_EQ(body[1]["content"], "first-question");
    EXPECT_EQ(body[2]["thinking"], "saved-thinking");
    EXPECT_EQ(body[2]["tool_calls"][0]["function"]["name"], "lookup");
    EXPECT_EQ(body[2]["tool_calls"][0]["function"]["arguments"]["item"], "example");
    EXPECT_EQ(body[3]["content"], "saved-tool-result");
    EXPECT_EQ(body[4]["content"], "first-followup");
    ASSERT_TRUE(provider.bindSession(manager, second));
    EXPECT_FALSE(provider.bindSession(manager, "missing"));
    EXPECT_THROW(provider.sendMessage("second-followup"), std::runtime_error);
    body = provider.request["messages"];
    ASSERT_EQ(body.size(), 3u);
    EXPECT_EQ(body[0]["content"], "second-system");
    EXPECT_EQ(body[1]["content"], "second-question");
    EXPECT_EQ(body[2]["content"], "second-followup");
    ASSERT_TRUE(provider.bindSession(manager, first));
    EXPECT_EQ(provider.getHistory().size(), 3u);
    EXPECT_EQ(manager->getMessages(first)->size(), 4u);
    EXPECT_EQ(manager->getMessages(second)->size(), 2u);
}

TEST(SessionSafety, DefaultsAndIdempotentMetadata) {
    Message message;
    EXPECT_EQ(message.role, MessageRole::User);
    EXPECT_EQ(message.createdAt, 0);
    SessionManager manager;
    auto c = config();
    auto id = manager.createSession("user", c.configId);
    message.messageId = "stable-id";
    message.content = "hello";
    message.modelName = "actual-model";
    message.modelConfigId = "actual-config";
    message.thinkingEnabled = false;
    c.thinkingEnabled = true;
    ASSERT_TRUE(manager.addMessages(id, {message, message}, c));
    ASSERT_TRUE(manager.addMessage(id, message, c));
    auto messages = manager.getMessages(id).value();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_GT(messages[0].createdAt, 0);
    EXPECT_EQ(messages[0].modelName, "actual-model");
    EXPECT_EQ(messages[0].thinkingEnabled, false);
    EXPECT_EQ(manager.getSession(id)->modelConfigId, "actual-config");
    ASSERT_TRUE(manager.clearMessages(id));
    EXPECT_FALSE(manager.addMessages(id, {message}, c, 0));
    EXPECT_TRUE(manager.getMessages(id)->empty());
}
TEST(SessionSafety, ConcurrentDeduplicationAndSnapshots) {
    SessionManager manager;
    auto c = config();
    auto id = manager.createSession("u", c.configId);
    Message message; message.messageId = "same";
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i)
        workers.emplace_back([&] { for (int j = 0; j < 100; ++j) {
            EXPECT_TRUE(manager.addMessage(id, message, c));
            EXPECT_EQ(manager.getMessages(id)->size(), 1u);
        }});
    for (auto& worker : workers) worker.join();
    auto snapshot = manager.getMessages(id).value();
    manager.deleteSession(id);
    EXPECT_EQ(snapshot.size(), 1u);
}
TEST(SessionSafety, ApiKeyUpdatePreservesInitializationState) {
    OpenAICompatibleProvider provider;
    ModelConfig unset;
    EXPECT_EQ(unset.timeoutSeconds, 0);
    EXPECT_FALSE(unset.stream);
    EXPECT_FALSE(provider.initialize(unset));
    auto c = config(ApiProtocol::OpenAICompatible);
    EXPECT_FALSE(provider.initialize(c));
    const auto initializationError = provider.getLastError();
    provider.setApiKey("test-only-placeholder");
    EXPECT_EQ(provider.getApiKey(), "test-only-placeholder");
    EXPECT_FALSE(provider.isInitialized());
    EXPECT_EQ(provider.getLastError(), initializationError);
    c.apiKey = provider.getApiKey();
    ASSERT_TRUE(provider.initialize(c));
    provider.setApiKey("replacement-placeholder");
    EXPECT_EQ(provider.getApiKey(), "replacement-placeholder");
    EXPECT_TRUE(provider.isInitialized());
    EXPECT_TRUE(provider.getLastError().empty());
    provider.setApiKey("");
    EXPECT_TRUE(provider.getApiKey().empty());
    EXPECT_TRUE(provider.isInitialized());
    c.timeoutSeconds = 0;
    EXPECT_FALSE(provider.initialize(c));
    provider.setApiKey("test-only-placeholder");
    EXPECT_FALSE(provider.isInitialized());
    EXPECT_NE(provider.getLastError().find("Timeout"), std::string::npos);
}
TEST(SessionSafety, ForeignNativeDataFallsBackToCommonFields) {
    Inspect<OpenAICompatibleProvider> openai;
    Inspect<GeminiProvider> gemini;
    Message message; message.role = MessageRole::Assistant; message.content = "retained";
    message.protocolData = R"({"parts":[{"text":"retained","thoughtSignature":"opaque"}]})";
    EXPECT_EQ(openai.body({message}, true)["input"][0]["content"], "retained");
    EXPECT_EQ(gemini.body({message})["contents"][0]["parts"][0]["thoughtSignature"], "opaque");
    message.protocolData = R"({"output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"retained"}]}]})";
    EXPECT_EQ(gemini.body({message})["contents"][0]["parts"][0]["text"], "retained");
    EXPECT_EQ(openai.body({message}, true)["input"][0]["type"], "message");
    message.modelName = "other-model";
    EXPECT_EQ(openai.body({message}, true)["input"][0]["content"], "retained");
}
TEST(SessionSafety, ForeignToolIdsArePairedWithoutChangingStoredMessages) {
    Inspect<OpenAICompatibleProvider> openai;
    Message call; call.role = MessageRole::Assistant;
    call.toolCalls = {{"", "value", "{}"}, {"", "value", "{}"}};
    Message result; result.role = MessageRole::Tool; result.toolName = "value"; result.content = "ok";
    auto body = openai.body({call, result, result});
    EXPECT_FALSE(body["messages"][0]["tool_calls"][0]["id"].asString().empty());
    EXPECT_EQ(body["messages"][0]["tool_calls"][0]["id"], body["messages"][1]["tool_call_id"]);
    EXPECT_EQ(body["messages"][0]["tool_calls"][1]["id"], body["messages"][2]["tool_call_id"]);
    EXPECT_TRUE(call.toolCalls[0].id.empty());
}
TEST(SessionSafety, OllamaChunksAppendAndChatChunksMerge) {
    Message reply;
    auto chunk = parseJson(R"({"tool_calls":[{"function":{"name":"value","arguments":{"value":1}}}]})");
    readToolCalls(chunk["tool_calls"], true, true, reply);
    chunk["tool_calls"][0]["function"]["arguments"]["value"] = 2;
    readToolCalls(chunk["tool_calls"], true, true, reply);
    ASSERT_EQ(reply.toolCalls.size(), 2u);
    EXPECT_NE(reply.toolCalls[0].arguments, reply.toolCalls[1].arguments);
    reply = Message{};
    chunk = parseJson(R"({"tool_calls":[{"index":0,"id":"call","function":{"name":"value","arguments":"{\"value\":"}}]})");
    readToolCalls(chunk["tool_calls"], true, false, reply);
    chunk = parseJson(R"({"tool_calls":[{"index":0,"function":{"arguments":"1}"}}]})");
    readToolCalls(chunk["tool_calls"], true, false, reply);
    ASSERT_EQ(reply.toolCalls.size(), 1u);
    EXPECT_EQ(reply.toolCalls[0].arguments, "{\"value\":1}");
}
TEST_F(HttpHistory, RestoreSwitchAndAutomaticSave) {
    auto manager = std::make_shared<SessionManager>();
    auto c = config(); auto id = manager->createSession("u", c.configId, "", "system");
    OllamaProvider first; ASSERT_TRUE(first.initialize(c)); ASSERT_TRUE(first.bindSession(manager, id));
    auto result = first.sendMessage("first");
    EXPECT_EQ(manager->getMessages(id)->size(), 3u);
    auto wrong = c; wrong.configId = "wrong"; wrong.model.modelName = "wrong";
    manager->addMessages(id, result.messages, wrong);
    EXPECT_EQ(manager->getMessages(id)->size(), 3u);
    EXPECT_EQ(manager->getSession(id)->modelConfigId, "original");
    OllamaProvider second; c.model.modelName = "new-model"; c.configId = "new";
    ASSERT_TRUE(second.bindSession(manager, id)); ASSERT_TRUE(second.initialize(c));
    const auto next = second.sendMessage("second");
    const auto request = parseJson(next.content);
    ASSERT_EQ(request["messages"].size(), 4u);
    EXPECT_EQ(request["messages"][0]["content"], "system");
    EXPECT_EQ(request["messages"][1]["content"], "first");
    EXPECT_EQ(next.messages[0].modelName, "new-model");
    second.setSystemPrompt("updated"); second.clearHistory();
    EXPECT_TRUE(second.getHistory().empty());
    ASSERT_EQ(manager->getMessages(id)->size(), 1u);
    EXPECT_EQ(manager->getMessages(id)->front().content, "updated");
}
TEST_F(HttpHistory, ClearFromAnotherProviderRejectsStaleWrite) {
    auto manager = std::make_shared<SessionManager>();
    auto c = config(); auto id = manager->createSession("u", c.configId);
    PausedProvider first; OllamaProvider second;
    ASSERT_TRUE(first.initialize(c)); ASSERT_TRUE(second.initialize(c));
    ASSERT_TRUE(first.bindSession(manager,id)); ASSERT_TRUE(second.bindSession(manager,id));
    auto pending = std::async(std::launch::async, [&] { return first.sendMessage("old"); });
    auto entered = first.entered.get_future();
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    second.clearHistory(); first.release.set_value();
    EXPECT_FALSE(pending.get().content.empty());
    EXPECT_TRUE(manager->getMessages(id)->empty());
}
TEST_F(HttpHistory, ConcurrentSendAndConfigurationSnapshot) {
    PausedProvider provider; auto c = config(); ASSERT_TRUE(provider.initialize(c));
    auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("old"); });
    EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    auto concurrent = std::async(std::launch::async, [&] { return provider.sendMessage("concurrent"); });
    EXPECT_EQ(concurrent.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_FALSE(concurrent.get().content.empty());
    c.model.modelName = "new-model"; c.configId = "new"; ASSERT_TRUE(provider.initialize(c));
    provider.release.set_value(); auto old = pending.get();
    EXPECT_EQ(old.messages[0].modelName, "test-model");
    EXPECT_EQ(old.messages[0].modelConfigId, "original");
    EXPECT_TRUE(provider.getHistory().empty());
}
TEST_F(HttpHistory, ConcurrentTurnsCommitWithoutLoss) {
    PausedProvider provider; ASSERT_TRUE(provider.initialize(config()));
    auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("first"); });
    EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    auto next = provider.sendMessage("second");
    provider.release.set_value(); auto first = pending.get();
    auto history = provider.getHistory();
    ASSERT_EQ(history.size(), 4u);
    EXPECT_EQ(history[0].messageId, next.messages[0].messageId);
    EXPECT_EQ(history[2].messageId, first.messages[0].messageId);
}
TEST_F(HttpHistory, RebindingAndDeletionDoNotRedirectOldResults) {
    for (bool remove : {false, true}) {
        auto manager = std::make_shared<SessionManager>();
        auto c = config(); auto old = manager->createSession("u", c.configId);
        auto next = manager->createSession("u", c.configId);
        PausedProvider provider; ASSERT_TRUE(provider.initialize(c));
        ASSERT_TRUE(provider.bindSession(manager, old));
        EXPECT_FALSE(provider.bindSession(manager, "missing"));
        auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("old"); });
        EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
        if (remove) manager->deleteSession(old);
        else EXPECT_TRUE(provider.bindSession(manager, next));
        provider.release.set_value(); auto result = pending.get();
        EXPECT_FALSE(result.content.empty());
        EXPECT_TRUE(manager->getMessages(next)->empty());
        if (remove) {
            EXPECT_FALSE(manager->getSession(old));
            EXPECT_THROW(provider.sendMessage("deleted"), std::logic_error);
        } else {
            auto saved = manager->getMessages(old).value();
            ASSERT_EQ(saved.size(), result.messages.size());
            EXPECT_EQ(saved.front().messageId, result.messages.front().messageId);
        }
    }
}
TEST_F(HttpHistory, BoundModelSwitchPreservesInFlightHistoryAndMetadata) {
    auto manager = std::make_shared<SessionManager>();
    auto c = config(); auto id = manager->createSession("u", c.configId);
    PausedProvider provider; ASSERT_TRUE(provider.initialize(c));
    ASSERT_TRUE(provider.bindSession(manager, id));
    auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("old-model"); });
    EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    c.configId = "new-config"; c.model.modelName = "new-model";
    ASSERT_TRUE(provider.initialize(c));
    auto next = provider.sendMessage("new-model");
    provider.release.set_value(); auto old = pending.get();
    auto saved = manager->getMessages(id).value();
    ASSERT_EQ(saved.size(), 4u);
    EXPECT_EQ(saved[0].messageId, next.messages[0].messageId);
    EXPECT_EQ(saved[0].modelConfigId, "new-config");
    EXPECT_EQ(saved[2].messageId, old.messages[0].messageId);
    EXPECT_EQ(saved[2].modelConfigId, "original");
    EXPECT_EQ(saved[2].modelName, "test-model");
    EXPECT_EQ(provider.getHistory().size(), 4u);
}
TEST_F(HttpHistory, FailedModelSwitchDoesNotDiscardOriginalSessionResult) {
    auto manager = std::make_shared<SessionManager>();
    auto c = config(); auto id = manager->createSession("u", c.configId);
    PausedProvider provider; ASSERT_TRUE(provider.initialize(c));
    ASSERT_TRUE(provider.bindSession(manager, id));
    auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("valid-model"); });
    EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    c.timeoutSeconds = 0; EXPECT_FALSE(provider.initialize(c));
    provider.release.set_value(); auto result = pending.get();
    EXPECT_FALSE(provider.isInitialized());
    ASSERT_EQ(manager->getMessages(id)->size(), 2u);
    EXPECT_EQ(manager->getMessages(id)->front().messageId, result.messages.front().messageId);
}
TEST_F(HttpHistory, ApiKeyChangesOnlySubsequentRequests) {
    PausedProvider provider; auto c = config(); c.apiKey = "old-test-key";
    ASSERT_TRUE(provider.initialize(c));
    auto pending = std::async(std::launch::async, [&] { return provider.sendMessage("old-key"); });
    EXPECT_EQ(provider.entered.get_future().wait_for(std::chrono::seconds(3)), std::future_status::ready);
    provider.setApiKey("new-test-key");
    provider.release.set_value(); auto old = pending.get();
    auto next = provider.sendMessage("new-key");
    EXPECT_EQ(parseJson(old.content)["observedAuthorization"], "Bearer old-test-key");
    EXPECT_EQ(parseJson(next.content)["observedAuthorization"], "Bearer new-test-key");
    EXPECT_EQ(provider.getHistory().size(), 4u);
}
TEST_F(HttpHistory, StreamingToolsAndReentrantCallback) {
    OllamaProvider provider; ASSERT_TRUE(provider.initialize(config()));
    std::vector<int> executions;
    provider.setTools({{"value", "return value", "{}", [&](const std::string& args) {
        auto n = parseJson(args)["value"].asInt(); executions.push_back(n); return std::to_string(n);
    }}});
    int done = 0;
    auto result = provider.sendMessageStream("tools", [&](const std::string&, bool finished) {
        provider.getHistory(); if (finished) ++done;
    });
    EXPECT_EQ(executions, (std::vector<int>{1, 2}));
    EXPECT_EQ(done, 1);
    ASSERT_EQ(result.messages.size(), 5u);
    EXPECT_EQ(result.messages[2].content, "1");
    EXPECT_EQ(result.messages[3].content, "2");
}
} // namespace

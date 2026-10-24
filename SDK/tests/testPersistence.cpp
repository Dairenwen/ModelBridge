#include "DataManager.hpp"
#include "LLMProvider.hpp"
#include "SessionManager.hpp"
#include <gtest/gtest.h>
#include <json/json.h>
#include <sqlite3.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <unordered_set>

using namespace modelBridge;

namespace
{
    ModelConfig modelConfig()
    {
        ModelConfig config{};
        config.configId = "original";
        config.model.modelName = "test-model";
        config.model.protocol = ApiProtocol::Ollama;
        config.model.provider = ProviderType::Ollama;
        config.baseUrl = "http://127.0.0.1:11434";
        config.timeoutSeconds = 120;
        config.temperature = 0;
        config.maxOutputTokens = 40960;
        config.thinkingEnabled = false;
        return config;
    }

    Message message(const std::string &id, MessageRole role, const std::string &content)
    {
        Message result{};
        result.messageId = id;
        result.role = role;
        result.content = content;
        result.createdAt = 123456789;
        result.modelConfigId = "original";
        result.modelName = "test-model";
        return result;
    }

    // 逐字段比较两条消息是否一致
    void expectMessage(const Message &actual, const Message &expected)
    {
        EXPECT_EQ(actual.messageId, expected.messageId);
        EXPECT_EQ(actual.role, expected.role);
        EXPECT_EQ(actual.content, expected.content);
        EXPECT_EQ(actual.createdAt, expected.createdAt);
        EXPECT_EQ(actual.toolCallId, expected.toolCallId);
        EXPECT_EQ(actual.toolName, expected.toolName);
        EXPECT_EQ(actual.reasoningContent, expected.reasoningContent);
        EXPECT_EQ(actual.protocolData, expected.protocolData);
        EXPECT_EQ(actual.modelConfigId, expected.modelConfigId);
        EXPECT_EQ(actual.modelName, expected.modelName);
        EXPECT_EQ(actual.reasoningEffort, expected.reasoningEffort);
        EXPECT_EQ(actual.thinkingEnabled, expected.thinkingEnabled);
        ASSERT_EQ(actual.toolCalls.size(), expected.toolCalls.size());
        for (std::size_t i = 0; i < expected.toolCalls.size(); ++i)
        {
            SCOPED_TRACE(i);
            EXPECT_EQ(actual.toolCalls[i].id, expected.toolCalls[i].id);
            EXPECT_EQ(actual.toolCalls[i].name, expected.toolCalls[i].name);
            EXPECT_EQ(actual.toolCalls[i].arguments, expected.toolCalls[i].arguments);
        }
    }

    // 比较会话信息以及所有消息是否一致
    void expectSession(const Session &actual, const Session &expected)
    {
        EXPECT_EQ(actual.sessionId, expected.sessionId);
        EXPECT_EQ(actual.userId, expected.userId);
        EXPECT_EQ(actual.modelConfigId, expected.modelConfigId);
        EXPECT_EQ(actual.title, expected.title);
        EXPECT_EQ(actual.createdAt, expected.createdAt);
        EXPECT_EQ(actual.updatedAt, expected.updatedAt);
        EXPECT_EQ(actual.generation, expected.generation);
        ASSERT_EQ(actual.messages.size(), expected.messages.size());
        for (std::size_t i = 0; i < expected.messages.size(); ++i)
        {
            SCOPED_TRACE(i);
            expectMessage(actual.messages[i], expected.messages[i]);
        }
    }

    // 管理临时数据库以及测试环境
    class Persistence : public testing::Test
    {
    protected:
        std::filesystem::path databasePath;
        std::shared_ptr<DataManager> data;
        std::shared_ptr<SessionManager> sessions;
        ModelConfig config = modelConfig();

        // 创建临时数据库
        void SetUp() override
        {
            databasePath = std::filesystem::temp_directory_path() / (SessionManager::generateSessionId() + ".db");
            reopen();
        }
        // 及时删除测试用的临时数据库
        void TearDown() override
        {
            sessions.reset();
            data.reset();
            // 只删除本用例创建的明确文件，不删除目录或业务数据库。
            std::filesystem::remove(databasePath);
        }

        // 模拟重启，重新打开数据库，检验回滚效果
        void reopen()
        {
            sessions.reset();
            data.reset();
            data = std::make_shared<DataManager>(databasePath.string());
            sessions = std::make_shared<SessionManager>(data);
        }

        // 用于注入数据库故障和检查物理记录，独立连接
        int sql(const char *statement)
        {
            sqlite3 *connection = nullptr;
            const int opened = sqlite3_open(databasePath.string().c_str(), &connection);
            std::unique_ptr<sqlite3, decltype(&sqlite3_close)> handle(connection, sqlite3_close);
            if (opened != SQLITE_OK)
                throw std::runtime_error("Cannot open test database");
            int value = 0;
            const auto collect = [](void *output, int, char **values, char **)
            {
                *static_cast<int *>(output) = std::stoi(values[0]);
                return 0;
            };
            if (sqlite3_exec(connection, statement, collect, &value, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(connection));
            return value;
        }

        // 检查一轮对话是否完整保存到数据库中
        void expectTurn(const std::string &id, const ChatResult &result, std::size_t offset)
        {
            const auto stored = data->getMessages(id);
            ASSERT_TRUE(stored);
            ASSERT_EQ(stored->size(), offset + result.messages.size());
            for (std::size_t i = 0; i < result.messages.size(); ++i)
                expectMessage((*stored)[offset + i], result.messages[i]);
        }
    };

    // 测试数据库重新打开后，所有会话字段、消息内容和消息顺序是否完整保留。
    TEST_F(Persistence, ReopenPreservesEveryFieldAndMessageOrder)
    {
        const auto id = sessions->createSession("user", config.configId, "中文标题 ' \"", "system");
        auto expected = sessions->getSession(id).value();
        auto user = message("user", MessageRole::User, "你好\n\"历史\"");
        user.content.push_back('\0');
        user.content += "末尾";
        auto call = message("call", MessageRole::Assistant, "");
        call.toolCalls = {{"call-a", "lookup", R"({ "city": "武汉", "escaped": "\u4e2d" })"},
                          {"call-b", "lookup", "{\n  \"city\": \"北京\"\n}"}};
        call.reasoningContent = "思考\n原文";
        call.protocolData = R"({ "parts": [{"thoughtSignature":"opaque+/==","thought":true}] })";
        call.reasoningEffort = "high";
        call.thinkingEnabled = true;
        auto first = message("tool-a", MessageRole::Tool, R"({"result":"晴天"})");
        first.toolCallId = "call-a";
        first.toolName = "lookup";
        first.createdAt = 10; // 顺序由追加位置决定，不能按创建时间重排。
        auto second = message("tool-b", MessageRole::Tool, "");
        second.toolCallId = "call-b";
        second.toolName = "lookup";
        second.reasoningEffort = "";
        second.thinkingEnabled = false;
        auto answer = message("answer", MessageRole::Assistant, "完整回答");
        answer.modelConfigId = "switched";
        answer.modelName = "other-model";
        answer.protocolData = R"({"output":[{"type":"reasoning","encrypted_content":"signature"}]})";
        const std::vector<Message> turn{user, call, first, second, answer};
        ASSERT_TRUE(sessions->addMessages(id, turn, config));
        expected.messages.insert(expected.messages.end(), turn.begin(), turn.end());
        expected.modelConfigId = answer.modelConfigId;
        expected.updatedAt = sessions->getSession(id)->updatedAt;
        expectSession(data->getSession(id).value(), expected);

        reopen();
        EXPECT_EQ(sessions->getSessionCount(), 0u);
        ASSERT_TRUE(sessions->restoreSession(id));
        EXPECT_EQ(sessions->getSessionCount(), 1u);
        expectSession(sessions->getSession(id).value(), expected);
        expectSession(data->getSession(id).value(), expected); // 读取不刷新活动时间。
        EXPECT_THROW(data->createSession(expected), std::runtime_error);
        expectSession(data->getSession(id).value(), expected);
    }
    // 测试重复提交消息时，不会重复插入或覆盖已有消息，并正确记录最新插入消息的模型信息。
    TEST_F(Persistence, DuplicateSubmissionsPreserveContentAndLatestInsertedModel)
    {
        const auto id = sessions->createSession("user", config.configId);
        const auto first = message("first", MessageRole::User, "original");
        ASSERT_TRUE(sessions->addMessage(id, first, config));
        auto next = message("next", MessageRole::Assistant, "next");
        next.modelConfigId = "new-config";
        next.modelName = "new-model";
        next.thinkingEnabled = false;
        ASSERT_EQ(data->appendMessages(id, {first, next, first}, 900), DataManager::WriteResult::Success);
        const auto expected = data->getSession(id).value();
        EXPECT_EQ(expected.modelConfigId, "new-config");
        ASSERT_EQ(expected.messages.size(), 2u);
        auto changed = first;
        changed.content = "must not overwrite";
        ASSERT_TRUE(sessions->addMessages(id, {changed, next, next}, config));
        ASSERT_TRUE(sessions->addMessages(id, {}, config));
        ASSERT_EQ(data->appendMessages(id, {changed}, 999), DataManager::WriteResult::Success);
        reopen();
        expectSession(sessions->getSession(id).value(), expected);
    }
    // 测试不同用户的会话隔离、查询排序、标题修改、系统提示词更新和删除操作。
    TEST_F(Persistence, ListsAndMutationsRemainUserScoped)
    {
        const auto first = sessions->createSession("alice", config.configId, "first", "old-system");
        const auto second = sessions->createSession("alice", config.configId, "second");
        const auto other = sessions->createSession("bob", config.configId, "other", "bob-system");
        ASSERT_TRUE(data->updateSessionTimestamp(first, 100));
        ASSERT_TRUE(data->updateSessionTimestamp(second, 100));
        auto list = sessions->getSessions("alice");
        ASSERT_EQ(list.size(), 2u);
        EXPECT_LT(list[0].sessionId, list[1].sessionId);
        EXPECT_TRUE(list[0].messages.empty());
        EXPECT_TRUE(list[1].messages.empty());
        EXPECT_TRUE(sessions->getSessions("alice' OR 1=1 --").empty());
        ASSERT_TRUE(data->updateSessionTimestamp(second, 200));
        EXPECT_EQ(sessions->getSessions("alice").front().sessionId, second);

        const auto user = message("question", MessageRole::User, "keep-history");
        ASSERT_TRUE(sessions->addMessage(first, user, config));
        ASSERT_TRUE(sessions->setSystemPrompt(first, "new-system"));
        ASSERT_TRUE(sessions->updateSessionTitle(first, "改名 ' \""));
        ASSERT_TRUE(sessions->updateSessionTimestamp(first));
        auto expected = sessions->getSession(first).value();
        EXPECT_EQ(expected.title, "改名 ' \"");
        ASSERT_EQ(expected.messages.size(), 2u);
        EXPECT_EQ(expected.messages.front().content, "new-system");
        expectMessage(expected.messages.back(), user);
        reopen();
        expectSession(sessions->getSession(first).value(), expected);
        ASSERT_TRUE(sessions->setSystemPrompt(first, ""));
        ASSERT_EQ(sessions->getMessages(first)->size(), 1u);
        expectMessage(sessions->getMessages(first)->front(), user);
        EXPECT_THROW(sessions->clearSessions(), std::logic_error);
        sessions->clearSessions("alice");
        reopen();
        EXPECT_TRUE(sessions->getSessions("alice").empty());
        ASSERT_TRUE(sessions->getSession(other));
        EXPECT_EQ(sql("SELECT COUNT(*) FROM messages"), 1);
        ASSERT_TRUE(sessions->deleteSession(other));
        EXPECT_FALSE(sessions->deleteSession(other));
        EXPECT_EQ(sql("SELECT COUNT(*) FROM messages"), 0);
    }
    // 测试会话被清空或删除后，其他数据库连接不能继续写入过期消息。
    TEST_F(Persistence, ClearAndDeleteRejectStaleWritesAcrossConnections)
    {
        const auto id = sessions->createSession("user", config.configId, "", "system");
        const auto user = message("old", MessageRole::User, "old request");
        ASSERT_TRUE(sessions->addMessage(id, user, config));
        const auto before = sessions->getSession(id).value();
        {
            DataManager other(databasePath.string());
            ASSERT_TRUE(other.clearMessages(id, 500));
            EXPECT_EQ(data->appendMessage(id, user, 600, before.generation), DataManager::WriteResult::VersionMismatch);
            EXPECT_FALSE(sessions->addMessages(id, {user}, config, before.generation));
        }
        reopen();
        auto cleared = sessions->getSession(id).value();
        EXPECT_EQ(cleared.generation, before.generation + 1);
        ASSERT_EQ(cleared.messages.size(), 1u);
        expectMessage(cleared.messages.front(), before.messages.front());
        ASSERT_TRUE(sessions->addMessages(id, {user}, config, cleared.generation));
        ASSERT_TRUE(sessions->clearMessages(id));
        EXPECT_EQ(sessions->getSession(id)->generation, cleared.generation + 1);
        {
            DataManager other(databasePath.string());
            ASSERT_TRUE(other.deleteSession(id));
            EXPECT_EQ(data->appendMessage(id, user, 700), DataManager::WriteResult::NotFound);
            EXPECT_FALSE(sessions->addMessage(id, user, config));
        }
        reopen();
        EXPECT_FALSE(sessions->restoreSession(id));
        EXPECT_FALSE(sessions->getMessages(id));
        EXPECT_EQ(sql("SELECT COUNT(*) FROM messages"), 0);
    }
    // 模拟数据库操作失败，验证事务是否正确回滚，不留下部分数据。
    TEST_F(Persistence, FailedTransactionsLeaveNoPartialMessagesOrSessions)
    {
        const auto id = sessions->createSession("user", config.configId, "", "system");
        const auto before = sessions->getSession(id).value();
        sql("CREATE TRIGGER reject_message BEFORE INSERT ON messages WHEN NEW.messageId='reject' "
            "BEGIN SELECT RAISE(ABORT, 'test write failure'); END");
        const auto good = message("good", MessageRole::User, "must roll back");
        const auto bad = message("reject", MessageRole::Assistant, "rejected");
        EXPECT_THROW(sessions->addMessages(id, {good, bad}, config), std::runtime_error);
        auto prompt = message("reject", MessageRole::System, "must not replace old system");
        EXPECT_THROW(data->setSystemPrompt(id, prompt, 800), std::runtime_error);
        expectSession(sessions->getSession(id).value(), before);
        reopen();
        expectSession(sessions->getSession(id).value(), before);
        sql("DROP TRIGGER reject_message");
        ASSERT_TRUE(sessions->addMessage(id, good, config));
        sql("CREATE TRIGGER reject_session BEFORE INSERT ON sessions "
            "BEGIN SELECT RAISE(ABORT, 'test creation failure'); END");
        EXPECT_THROW(sessions->createSession("user", config.configId), std::runtime_error);
        EXPECT_EQ(sessions->getSessionCount(), 1u);
        EXPECT_EQ(sql("SELECT COUNT(*) FROM sessions"), 1);
        sql("DROP TRIGGER reject_session");
        // 查询失败必须反馈错误，不能变成空历史。
        sql("ALTER TABLE messages RENAME TO unavailable_messages");
        EXPECT_THROW(sessions->getMessages(id), std::runtime_error);
        sql("ALTER TABLE unavailable_messages RENAME TO messages");
        EXPECT_EQ(sessions->getMessages(id)->size(), 2u);
    }
    // 使用 4 个线程和 2 个数据库连接并发写入，验证消息不丢失、不重复，且每轮消息保持完整。
    TEST_F(Persistence, ConcurrentConnectionsCommitWholeTurnsWithoutDuplicates)
    {
        const auto id = sessions->createSession("user", config.configId);
        auto otherData = std::make_shared<DataManager>(databasePath.string());
        auto otherSessions = std::make_shared<SessionManager>(otherData);
        std::vector<std::future<bool>> workers;
        for (int worker = 0; worker < 4; ++worker)
        {
            const auto manager = worker % 2 ? otherSessions : sessions;
            workers.push_back(std::async(std::launch::async, [&, worker, manager]
                                         {
                for (int turn = 0; turn < 8; ++turn)
                {
                    const auto prefix = std::to_string(worker) + "-" + std::to_string(turn);
                    const std::vector<Message> messages{
                        message(prefix + "-user", MessageRole::User, prefix),
                        message(prefix + "-assistant", MessageRole::Assistant, prefix)};
                    if (!manager->addMessages(id, messages, config) || !manager->addMessages(id, messages, config))
                        return false;
                }
                return true; }));
        }
        for (auto &worker : workers)
            EXPECT_TRUE(worker.get());
        otherSessions.reset();
        otherData.reset();
        const auto expected = sessions->getSession(id).value();
        ASSERT_EQ(expected.messages.size(), 64u);
        std::unordered_set<std::string> ids;
        for (std::size_t i = 0; i < expected.messages.size(); i += 2)
        {
            EXPECT_EQ(expected.messages[i].role, MessageRole::User);
            EXPECT_EQ(expected.messages[i + 1].role, MessageRole::Assistant);
            EXPECT_EQ(expected.messages[i].content, expected.messages[i + 1].content);
            EXPECT_TRUE(ids.insert(expected.messages[i].messageId).second);
            EXPECT_TRUE(ids.insert(expected.messages[i + 1].messageId).second);
        }
        reopen();
        expectSession(sessions->getSession(id).value(), expected);
    }
    // 测试首次模型请求失败时，不会单独保存一条没有得到响应的用户消息。
    TEST_F(Persistence, FailedFirstRequestDoesNotPersistAnIsolatedUserMessage)
    {
        const auto id = sessions->createSession("user", config.configId, "", "system");
        const auto before = sessions->getSession(id).value();
        {
            OllamaProvider provider; // 未初始化，发送在 HTTP 之前失败。
            ASSERT_TRUE(provider.bindSession(sessions, id));
            EXPECT_THROW(provider.sendMessage("not sent"), std::logic_error);
        }
        reopen();
        expectSession(sessions->getSession(id).value(), before);
    }
    // 测试非流式和流式工具调用的完整记录能否保存、恢复，并在恢复后继续使用历史结果。
    class OllamaPersistence : public Persistence
    {
    protected:
        void SetUp() override
        {
            Persistence::SetUp();
            const auto *url = std::getenv("MODELBRIDGE_LIVE_OLLAMA_URL");
            const auto *model = std::getenv("MODELBRIDGE_LIVE_OLLAMA_MODEL");
            if (!url || !model)
                GTEST_SKIP() << "Set MODELBRIDGE_LIVE_OLLAMA_URL and MODELBRIDGE_LIVE_OLLAMA_MODEL";
            config.baseUrl = url;
            config.model.modelName = model;
        }
    };
    // 测试非流式对话保存后，关闭旧对象、重新打开数据库，能否恢复上下文并继续流式对话。
    TEST_F(OllamaPersistence, FullAndStreamResumeAfterClosingEveryOldObject)
    {
        const auto id = sessions->createSession("user", config.configId, "live", "Remember exact codes. Keep answers brief.");
        const auto code = "MB_" + std::to_string(currentTimeMillis());
        {
            OllamaProvider provider;
            ASSERT_TRUE(provider.initialize(config));
            ASSERT_TRUE(provider.bindSession(sessions, id));
            const auto result = provider.sendMessage("Remember this secret code: " + code + ". Reply only OK.");
            ASSERT_FALSE(result.content.empty());
            expectTurn(id, result, 1);
        }
        const auto before = sessions->getSession(id).value();
        reopen(); // 旧 Provider 已离开作用域，旧管理器和数据库连接均释放。
        ASSERT_EQ(sessions->getSessions("user").size(), 1u);
        ASSERT_TRUE(sessions->restoreSession(id));
        expectSession(sessions->getSession(id).value(), before);
        {
            OllamaProvider provider;
            ASSERT_TRUE(provider.initialize(config));
            ASSERT_TRUE(provider.bindSession(sessions, id));
            std::string text;
            int done = 0;
            const auto result = provider.sendMessageStream("What was the secret code? Reply with the code only.",
                                                           [&](const std::string &delta, bool finished)
                                                           {
                                                               text += delta;
                                                               provider.getHistory(); // 回调重入，验证持久化没有把回调包在锁内。
                                                               if (finished)
                                                               {
                                                                   ++done;
                                                                   EXPECT_EQ(data->getMessages(id)->size(), before.messages.size() + 2);
                                                               }
                                                           });
            EXPECT_EQ(done, 1);
            EXPECT_EQ(text, result.content);
            EXPECT_NE(result.content.find(code), std::string::npos) << result.content;
            expectTurn(id, result, before.messages.size());
        }
        const auto after = sessions->getSession(id).value();
        reopen();
        expectSession(sessions->getSession(id).value(), after);
    }
    // 测试非流式和流式工具调用的完整记录能否保存、恢复，并在恢复后继续使用历史结果。
    TEST_F(OllamaPersistence, FullAndStreamToolChainsSurviveReopen)
    {
        for (bool stream : {false, true})
        {
            SCOPED_TRACE(stream ? "stream" : "full");
            const auto id = sessions->createSession("user", config.configId, "tools", "Use tools when asked. Keep answers brief.");
            const auto code = "TOOL_" + std::to_string(currentTimeMillis());
            int executions = 0;
            {
                OllamaProvider provider;
                ASSERT_TRUE(provider.initialize(config));
                ASSERT_TRUE(provider.bindSession(sessions, id));
                provider.setTools({{"get_secret_code", "Read the secret code, available only through this tool.",
                                    R"({"type":"object","properties":{},"required":[]})",
                                    [&](const std::string &)
                                    { ++executions; return code; }}});
                const auto prompt = "Call get_secret_code, then output the exact code. Do not invent a code.";
                std::string text;
                int done = 0;
                const auto result = stream ? provider.sendMessageStream(prompt, [&](const std::string &delta, bool finished)
                                                                        {
                    text += delta;
                    if (finished) ++done; })
                                           : provider.sendMessage(prompt);
                ASSERT_GE(executions, 1);
                EXPECT_NE(result.content.find(code), std::string::npos) << result.content;
                std::size_t calls = 0, outputs = 0;
                for (const auto &entry : result.messages)
                {
                    calls += entry.toolCalls.size();
                    if (entry.role == MessageRole::Tool)
                    {
                        ++outputs;
                        EXPECT_EQ(entry.toolName, "get_secret_code");
                        EXPECT_EQ(entry.content, code);
                    }
                }
                EXPECT_EQ(calls, outputs);
                EXPECT_EQ(outputs, static_cast<std::size_t>(executions));
                if (stream)
                {
                    EXPECT_EQ(done, 1);
                    EXPECT_EQ(text, result.content);
                }
                expectTurn(id, result, 1);
                ASSERT_TRUE(sessions->addMessages(id, result.messages, config)); // 重复提交不能重复落库。
                expectTurn(id, result, 1);
            }
            const auto before = sessions->getSession(id).value();
            reopen();
            expectSession(sessions->getSession(id).value(), before);
            {
                OllamaProvider provider;
                ASSERT_TRUE(provider.initialize(config));
                ASSERT_TRUE(provider.bindSession(sessions, id));
                const auto result = provider.sendMessage("What code did the tool return previously? Reply with that exact code only.");
                EXPECT_NE(result.content.find(code), std::string::npos) << result.content;
                expectTurn(id, result, before.messages.size());
            }
        }
    }
    // 测试切换模型、切换会话后，各会话历史记录是否独立且完整。
    TEST_F(OllamaPersistence, ModelAndSessionSwitchesRetainTheirOwnHistory)
    {
        const auto *alternate = std::getenv("MODELBRIDGE_LIVE_OLLAMA_ALT_MODEL");
        if (!alternate)
            GTEST_SKIP() << "Set MODELBRIDGE_LIVE_OLLAMA_ALT_MODEL";
        auto switched = config;
        switched.configId = "switched";
        switched.model.modelName = alternate;
        switched.thinkingEnabled.reset();
        const auto first = sessions->createSession("user", config.configId);
        const auto second = sessions->createSession("user", config.configId);
        const auto firstCode = "FIRST_" + std::to_string(currentTimeMillis());
        const auto secondCode = "SECOND_" + std::to_string(currentTimeMillis());
        {
            OllamaProvider provider;
            ASSERT_TRUE(provider.initialize(config));
            ASSERT_TRUE(provider.bindSession(sessions, first));
            expectTurn(first, provider.sendMessage("Remember this code: " + firstCode + ". Reply OK."), 0);
            ASSERT_TRUE(provider.bindSession(sessions, second));
            expectTurn(second, provider.sendMessage("Remember this code: " + secondCode + ". Reply OK."), 0);
            ASSERT_TRUE(provider.initialize(switched));
            const auto result = provider.sendMessage("Repeat the code from this conversation. Reply with the code only.");
            EXPECT_NE(result.content.find(secondCode), std::string::npos) << result.content;
            EXPECT_EQ(result.messages.front().modelName, switched.model.modelName);
            expectTurn(second, result, 2);
        }
        const auto beforeFirst = sessions->getSession(first).value();
        const auto beforeSecond = sessions->getSession(second).value();
        reopen();
        expectSession(sessions->getSession(first).value(), beforeFirst);
        expectSession(sessions->getSession(second).value(), beforeSecond);
        {
            OllamaProvider provider;
            ASSERT_TRUE(provider.initialize(switched));
            ASSERT_TRUE(provider.bindSession(sessions, first));
            const auto result = provider.sendMessage("Repeat the code from this conversation. Reply with the code only.");
            EXPECT_NE(result.content.find(firstCode), std::string::npos) << result.content;
            expectTurn(first, result, beforeFirst.messages.size());
            EXPECT_EQ(sessions->getSession(first)->modelConfigId, switched.configId);
        }
        expectSession(sessions->getSession(second).value(), beforeSecond);
    }
    // 测试流式响应过程中切换会话、清空历史或删除会话时，是否会错误保存消息。
    TEST_F(OllamaPersistence, InFlightStreamKeepsOriginalSessionAndRejectsClearedOrDeletedHistory)
    {
        for (int action = 0; action < 3; ++action)
        {
            SCOPED_TRACE(action); // 依次验证切换绑定、清空、删除。
            const auto id = sessions->createSession("user", config.configId, "", "Keep answers brief.");
            const auto next = sessions->createSession("user", config.configId);
            bool changed = false;
            {
                OllamaProvider provider;
                ASSERT_TRUE(provider.initialize(config));
                ASSERT_TRUE(provider.bindSession(sessions, id));
                const auto result = provider.sendMessageStream("Reply with exactly: alpha beta gamma", [&](const std::string &delta, bool)
                                                               {
                    if (changed || delta.empty()) return;
                    changed = true;
                    if (action == 0) EXPECT_TRUE(provider.bindSession(sessions, next));
                    else if (action == 1) provider.clearHistory();
                    else EXPECT_TRUE(sessions->deleteSession(id)); });
                ASSERT_TRUE(changed);
                if (action == 0)
                    expectTurn(id, result, 1);
            }
            reopen();
            EXPECT_TRUE(sessions->getMessages(next)->empty());
            if (action == 1)
            {
                const auto stored = sessions->getSession(id).value();
                EXPECT_EQ(stored.generation, 1u);
                ASSERT_EQ(stored.messages.size(), 1u);
                EXPECT_EQ(stored.messages.front().role, MessageRole::System);
            }
            else if (action == 2)
                EXPECT_FALSE(sessions->getSession(id));
        }
    }

    // 首次请求和工具执行使用真实模型；在后续 HTTP 之前注入失败，不改变生产接口。
    class FailedFollowup : public OllamaProvider
    {
    protected:
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const override
        {
            if (messages.back().role == MessageRole::Tool)
                throw std::runtime_error("Test failure after completed tool execution");
            return OllamaProvider::makeRequestBody(messages, stream, context);
        }
    };
    // 测试工具调用已经完成，但后续模型请求失败时，已完成的工具调用记录是否仍能保留。
    TEST_F(OllamaPersistence, CompletedToolsSurviveFailedFollowup)
    {
        const auto id = sessions->createSession("user", config.configId);
        int executions = 0;
        {
            FailedFollowup provider;
            ASSERT_TRUE(provider.initialize(config));
            ASSERT_TRUE(provider.bindSession(sessions, id));
            provider.setTools({{"get_secret_code", "Read a secret code available only through this tool.",
                                R"({"type":"object","properties":{},"required":[]})",
                                [&](const std::string &)
                                { ++executions; return "saved-tool-result"; }}});
            EXPECT_THROW(provider.sendMessage("Call get_secret_code to read the secret code. Do not invent a code."), std::runtime_error);
        }
        ASSERT_GE(executions, 1);
        const auto before = sessions->getSession(id).value();
        ASSERT_GE(before.messages.size(), 3u);
        EXPECT_EQ(before.messages.back().role, MessageRole::Tool);
        EXPECT_EQ(before.messages.back().content, "saved-tool-result");
        reopen();
        expectSession(sessions->getSession(id).value(), before);
    }
    // 测试流式响应保存数据库失败时，不应向调用者发送成功完成的回调。
    TEST_F(OllamaPersistence, DatabaseFailureDoesNotReportStreamCompletion)
    {
        const auto id = sessions->createSession("user", config.configId);
        const auto before = sessions->getSession(id).value();
        sql("CREATE TRIGGER reject_message BEFORE INSERT ON messages "
            "BEGIN SELECT RAISE(ABORT, 'test save failure'); END");
        int done = 0;
        {
            OllamaProvider provider;
            ASSERT_TRUE(provider.initialize(config));
            ASSERT_TRUE(provider.bindSession(sessions, id));
            EXPECT_THROW(provider.sendMessageStream("Reply only OK.", [&](const std::string &, bool finished)
                                                    {
                if (finished) ++done; }),
                         std::runtime_error);
        }
        EXPECT_EQ(done, 0);
        reopen();
        expectSession(sessions->getSession(id).value(), before);
    }
} // namespace

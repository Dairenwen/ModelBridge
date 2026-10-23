#include "SessionManager.hpp"
#include <algorithm>
#include <atomic>
#include <iterator>
#include <mutex>
#include <random>
#include <utility>
#include <unordered_set>

namespace modelBridge
{
    std::string SessionManager::createSession(const std::string &userId, const std::string &modelConfigId, const std::string &title, const std::string &systemPrompt)
    {
        Session session{};
        session.sessionId = generateSessionId();
        session.userId = userId;
        session.modelConfigId = modelConfigId;
        session.title = title;
        session.createdAt = currentTimeMillis();
        session.updatedAt = session.createdAt;
        if (!systemPrompt.empty())
        {
            Message message{{}, MessageRole::System, systemPrompt, session.createdAt, {}, {}};
            message.messageId = generateMessageId();
            message.modelConfigId = modelConfigId;
            session.messages.push_back(std::move(message));
        }
        const auto sessionId = session.sessionId;
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        sessions.emplace(sessionId, std::move(session));
        return sessionId;
    }

    std::optional<Session> SessionManager::getSession(const std::string &sessionId) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return std::nullopt;
        return iterator->second;
    }

    bool SessionManager::addMessage(const std::string &sessionId, Message message, const ModelConfig &modelConfig)
    {
        return addMessages(sessionId, {std::move(message)}, modelConfig);
    }

    bool SessionManager::addMessages(const std::string &sessionId, std::vector<Message> messages, const ModelConfig &modelConfig, std::optional<std::uint64_t> expectedGeneration)
    {
        // 消息副本由本次调用独占；生成 ID 和补充字段时不阻塞其他会话。
        const auto now = currentTimeMillis();
        for (auto &message : messages)
        {
            if (message.messageId.empty())
                message.messageId = generateMessageId();
            if (message.createdAt == 0)
                message.createdAt = now;
            // Provider 已按请求快照填写的元数据不可被当前配置覆盖。
            if (message.modelName.empty())
            {
                message.modelConfigId = modelConfig.configId;
                message.modelName = modelConfig.model.modelName;
                message.reasoningEffort = modelConfig.reasoningEffort;
                message.thinkingEnabled = modelConfig.thinkingEnabled;
            }
        }

        std::unique_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return false;
        auto &session = iterator->second;
        if (expectedGeneration && *expectedGeneration != session.generation)
            return false;
        std::unordered_set<std::string> ids;
        for (const auto &message : session.messages)
            ids.insert(message.messageId);
        messages.erase(std::remove_if(messages.begin(), messages.end(), [&](const Message &message)
                       { return !ids.insert(message.messageId).second; }), messages.end());
        if (messages.empty())
            return true;
        auto modelConfigId = messages.back().modelConfigId;
        session.messages.insert(session.messages.end(), std::make_move_iterator(messages.begin()),
                                std::make_move_iterator(messages.end()));
        session.modelConfigId = std::move(modelConfigId);
        session.updatedAt = currentTimeMillis();
        return true;
    }

    bool SessionManager::clearMessages(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return false;
        auto &session = iterator->second;
        ++session.generation;
        session.messages.erase(std::remove_if(session.messages.begin(), session.messages.end(),
            [](const Message &message) { return message.role != MessageRole::System; }), session.messages.end());
        session.updatedAt = currentTimeMillis();
        return true;
    }

    bool SessionManager::setSystemPrompt(const std::string &sessionId, const std::string &prompt)
    {
        Message message{{}, MessageRole::System, prompt, currentTimeMillis()};
        if (!prompt.empty())
            message.messageId = generateMessageId();
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return false;
        auto &session = iterator->second;
        session.messages.erase(std::remove_if(session.messages.begin(), session.messages.end(),
            [](const Message &entry) { return entry.role == MessageRole::System; }), session.messages.end());
        if (!prompt.empty())
            session.messages.insert(session.messages.begin(), std::move(message));
        session.updatedAt = currentTimeMillis();
        return true;
    }

    std::optional<std::vector<Message>> SessionManager::getMessages(const std::string &sessionId) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return std::nullopt;
        return iterator->second.messages;
    }

    std::vector<Session> SessionManager::getAllSessions() const
    {
        std::vector<Session> result;
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex);
            result.reserve(sessions.size());
            for (const auto &entry : sessions)
                result.push_back(entry.second);
        }
        std::sort(result.begin(), result.end(), [](const Session &left, const Session &right)
                  { return left.updatedAt > right.updatedAt; });
        return result;
    }

    bool SessionManager::deleteSession(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        return sessions.erase(sessionId) != 0;
    }

    bool SessionManager::updateSessionTimestamp(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end())
            return false;
        iterator->second.updatedAt = currentTimeMillis();
        return true;
    }

    void SessionManager::clearSessions()
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        sessions.clear();
    }

    std::size_t SessionManager::getSessionCount() const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        return sessions.size();
    }

    std::string SessionManager::generateSessionId()
    {
        return generateId("session");
    }

    std::string SessionManager::generateMessageId()
    {
        return generateId("message");
    }

    std::string SessionManager::generateId(const char *prefix)
    {
        // 随机标识只初始化一次，降低不同进程间的冲突概率；清空会话不影响序号。
        static const auto processId = []
        {
            std::random_device random;
            return std::to_string(random()) + "-" + std::to_string(random());
        }();
        static std::atomic<std::uint64_t> sequence{0};
        return std::string(prefix) + "-" + std::to_string(currentTimeMillis()) + "-" + processId + "-" + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    }
} // namespace modelBridge

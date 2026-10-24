#include "SessionManager.hpp"
#include "DataManager.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <random>
#include <utility>
#include <unordered_set>
#include <stdexcept>

namespace modelBridge
{
    SessionManager::SessionManager(std::shared_ptr<DataManager> storage) : dataManager(std::move(storage)) {}

    Session *SessionManager::findSessionLocked(const std::string &sessionId)
    {
        if (dataManager)
        {
            auto stored = dataManager->getSession(sessionId);
            if (!stored)
            {
                sessions.erase(sessionId);
                return nullptr;
            }
            // 数据库为持久化模式的数据来源，不从旧缓存覆盖数据库历史。
            return &sessions.insert_or_assign(sessionId, std::move(*stored)).first->second;
        }
        const auto iterator = sessions.find(sessionId);
        return iterator == sessions.end() ? nullptr : &iterator->second;
    }

    bool SessionManager::restoreSession(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        return findSessionLocked(sessionId) != nullptr;
    }

    std::vector<Session> SessionManager::getSessions(const std::string &userId) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        if (dataManager) return dataManager->getSessions(userId);
        std::vector<Session> result;
        for (const auto &entry : sessions)
        {
            const auto &stored = entry.second;
            if (stored.userId != userId) continue;
            Session summary{};
            summary.sessionId = stored.sessionId;
            summary.userId = stored.userId;
            summary.modelConfigId = stored.modelConfigId;
            summary.title = stored.title;
            summary.createdAt = stored.createdAt;
            summary.updatedAt = stored.updatedAt;
            summary.generation = stored.generation;
            result.push_back(std::move(summary));
        }
        std::sort(result.begin(), result.end(), [](const Session &a, const Session &b) {
            return a.updatedAt != b.updatedAt ? a.updatedAt > b.updatedAt : a.sessionId < b.sessionId;
        });
        return result;
    }

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
        auto inserted = sessions.emplace(sessionId, std::move(session));
        if (!inserted.second) throw std::runtime_error("Generated session ID already exists");
        try
        {
            if (dataManager) dataManager->createSession(inserted.first->second);
        }
        catch (...)
        {
            sessions.erase(inserted.first);
            throw;
        }
        return sessionId;
    }

    std::optional<Session> SessionManager::getSession(const std::string &sessionId) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        if (dataManager) return dataManager->getSession(sessionId);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end()) return std::nullopt;
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
        auto *stored = findSessionLocked(sessionId);
        if (!stored) return false;
        auto &session = *stored;
        if (expectedGeneration && *expectedGeneration != session.generation)
            return false;
        std::unordered_set<std::string> ids;
        for (const auto &message : session.messages)
            ids.insert(message.messageId);
        messages.erase(std::remove_if(messages.begin(), messages.end(), [&](const Message &message)
                       { return !ids.insert(message.messageId).second; }), messages.end());
        if (messages.empty())
            return true;
        // 先准备可发布的完整内存记录；数据库失败时，原内存内容不变。
        Session updated = session;
        updated.modelConfigId = messages.back().modelConfigId;
        updated.updatedAt = currentTimeMillis();
        updated.messages.insert(updated.messages.end(), messages.begin(), messages.end());
        if (dataManager)
        {
            const auto result = dataManager->appendMessages(sessionId, messages, updated.updatedAt, session.generation);
            if (result != DataManager::WriteResult::Success) return false;
        }
        session = std::move(updated);
        return true;
    }

    bool SessionManager::clearMessages(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        auto *session = findSessionLocked(sessionId);
        if (!session) return false;
        auto updated = *session;
        ++updated.generation;
        updated.messages.erase(std::remove_if(updated.messages.begin(), updated.messages.end(),
            [](const Message &message) { return message.role != MessageRole::System; }), updated.messages.end());
        updated.updatedAt = currentTimeMillis();
        if (dataManager && !dataManager->clearMessages(sessionId, updated.updatedAt)) return false;
        *session = std::move(updated);
        return true;
    }

    bool SessionManager::setSystemPrompt(const std::string &sessionId, const std::string &prompt)
    {
        std::optional<Message> message;
        if (!prompt.empty())
        {
            message = Message{{}, MessageRole::System, prompt, currentTimeMillis()};
            message->messageId = generateMessageId();
        }
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        auto *session = findSessionLocked(sessionId);
        if (!session) return false;
        auto updated = *session;
        updated.messages.erase(std::remove_if(updated.messages.begin(), updated.messages.end(),
            [](const Message &entry) { return entry.role == MessageRole::System; }), updated.messages.end());
        if (message) updated.messages.insert(updated.messages.begin(), *message);
        updated.updatedAt = currentTimeMillis();
        if (dataManager && !dataManager->setSystemPrompt(sessionId, message, updated.updatedAt)) return false;
        *session = std::move(updated);
        return true;
    }

    std::optional<std::vector<Message>> SessionManager::getMessages(const std::string &sessionId) const
    {
        std::shared_lock<std::shared_mutex> lock(stateMutex);
        if (dataManager) return dataManager->getMessages(sessionId);
        const auto iterator = sessions.find(sessionId);
        if (iterator == sessions.end()) return std::nullopt;
        return iterator->second.messages;
    }

    std::vector<Session> SessionManager::getAllSessions() const
    {
        std::vector<Session> result;
        {
            std::shared_lock<std::shared_mutex> lock(stateMutex);
            result.reserve(sessions.size());
            for (const auto &entry : sessions)
            {
                if (!dataManager) result.push_back(entry.second);
                else if (auto stored = dataManager->getSession(entry.first)) result.push_back(std::move(*stored));
            }
        }
        std::sort(result.begin(), result.end(), [](const Session &left, const Session &right)
                  { return left.updatedAt > right.updatedAt; });
        return result;
    }

    bool SessionManager::updateSessionTitle(const std::string &sessionId, const std::string &title)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        auto *session = findSessionLocked(sessionId);
        if (!session) return false;
        auto updated = *session;
        updated.title = title;
        updated.updatedAt = currentTimeMillis();
        if (dataManager && !dataManager->updateSession(updated)) return false;
        *session = std::move(updated);
        return true;
    }

    bool SessionManager::deleteSession(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        const bool removed = dataManager ? dataManager->deleteSession(sessionId) : sessions.count(sessionId) != 0;
        sessions.erase(sessionId);
        return removed;
    }

    bool SessionManager::updateSessionTimestamp(const std::string &sessionId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        auto *session = findSessionLocked(sessionId);
        if (!session) return false;
        const auto now = currentTimeMillis();
        if (dataManager && !dataManager->updateSessionTimestamp(sessionId, now)) return false;
        session->updatedAt = now;
        return true;
    }

    void SessionManager::clearSessions()
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        if (dataManager) throw std::logic_error("Persistent session deletion requires clearSessions(userId)");
        sessions.clear();
    }

    void SessionManager::clearSessions(const std::string &userId)
    {
        std::unique_lock<std::shared_mutex> lock(stateMutex);
        if (dataManager) dataManager->clearSessions(userId);
        for (auto iterator = sessions.begin(); iterator != sessions.end();)
        {
            if (iterator->second.userId == userId) iterator = sessions.erase(iterator);
            else ++iterator;
        }
    }

    std::size_t SessionManager::getSessionCount() const
    {
        if (dataManager) return getAllSessions().size();
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

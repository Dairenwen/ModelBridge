#pragma once
#include "Common.hpp"
#include <cstddef>
#include <shared_mutex>
#include <unordered_map>

namespace modelBridge
{
    /**
     * @brief 在内存中管理多组会话及其完整消息，不负责模型请求和数据库存储。
     * 先创建会话，再调用 Provider；一个管理器可保存多个用户的会话
     * 内部使用一把读写锁保护会话数据，查询返回副本，可与增删操作并发调用。
     * 调用期间入参须保持有效且不被其他线程修改；对象销毁前须等待所有调用结束。
     */
    class SessionManager
    {
    public:
        /**
         * @brief 创建一个空会话，可在模型连接初始化之前调用。
         * @param userId 会话所属用户 ID。
         * @param modelConfigId 初始模型连接 ID。
         * @param title 会话标题，默认为空。
         * @param systemPrompt 系统提示词；非空时作为首条 System 消息保存。
         * @return 新建会话的唯一 ID。
         */
        std::string createSession(const std::string &userId, const std::string &modelConfigId, const std::string &title = "", const std::string &systemPrompt = "");

        /**
         * @brief 按 ID 获取只读会话，不改变更新时间。
         * @param sessionId 会话 ID。
         * @return 会话快照；不存在时返回 std::nullopt，快照不受后续修改和删除影响。
         */
        std::optional<Session> getSession(const std::string &sessionId) const;

        /**
         * @brief 向指定会话添加一条消息，并更新会话时间。
         * @param sessionId 会话 ID。
         * @param message 待保存的消息，缺少 ID 时分配 ID，原有内容和工具字段保留。
         * @param modelConfig 未携带模型元数据的消息使用此配置；已有请求快照保持不变。
         * @return 添加成功返回 true；会话不存在时返回 false。
         */
        bool addMessage(const std::string &sessionId, Message message, const ModelConfig &modelConfig);

        /**
         * @brief 按原有顺序保存同一次模型调用产生的完整消息链。
         * @param sessionId 会话 ID。
         * @param messages 本轮新增消息，例如 ChatResult::messages；不要重复传入全部历史。
         * 已有 ID 保留并用于去重；缺少 ID 时生成，重试时调用方须复用 ID；按值接收，允许传入本会话消息的副本。
         * @param modelConfig 用于补齐无模型信息的消息；Provider 返回的消息保留实际请求配置。
         * @param expectedGeneration 请求开始时的会话版本；未提供时不检查版本。
         * @return 会话存在且版本匹配时返回 true；否则 false。空列表和重复消息不改变会话。
         */
        bool addMessages(const std::string &sessionId, std::vector<Message> messages, const ModelConfig &modelConfig, std::optional<std::uint64_t> expectedGeneration = std::nullopt);

        /** @brief 清空指定会话的非 System 消息，并使旧请求失效；不存在返回 false。 */
        bool clearMessages(const std::string &sessionId);

        /** @brief 替换指定会话的系统提示，空字符串移除；不存在返回 false。 */
        bool setSystemPrompt(const std::string &sessionId, const std::string &prompt);

        /**
         * @brief 获取指定会话按对话顺序保存的所有消息，包括系统消息和工具消息。
         * @param sessionId 会话 ID。
         * @return 消息列表快照；会话不存在时返回 std::nullopt。
         */
        std::optional<std::vector<Message>> getMessages(const std::string &sessionId) const;

        /**
         * @brief 获取所有会话的副本，按最近更新时间从新到旧排列。
         * @return 会话列表；没有会话时返回空列表，修改副本不影响管理器。
         */
        std::vector<Session> getAllSessions() const;

        /**
         * @brief 删除指定会话及其所有消息。
         * @param sessionId 会话 ID。
         * @return 删除成功返回 true；会话不存在时返回 false。
         */
        bool deleteSession(const std::string &sessionId);

        /**
         * @brief 更新会话的最近活动时间，可在重新打开会话继续聊天时调用。
         * @param sessionId 会话 ID。
         * @return 更新成功返回 true；会话不存在时返回 false。
         */
        bool updateSessionTimestamp(const std::string &sessionId);

        /**
         * @brief 清空内存中的所有会话及其消息，不重置 ID 生成序号。
         * @return 无。
         */
        void clearSessions();

        /**
         * @brief 获取当前保存的会话总数。
         * @return 会话数量。
         */
        std::size_t getSessionCount() const;

        /**
         * @brief 生成带 session 前缀的会话 ID，不创建会话。
         * @return 新生成的会话 ID。
         */
        static std::string generateSessionId();

        /**
         * @brief 生成带 message 前缀的消息 ID，不添加消息。
         * @return 新生成的消息 ID，与工具协议中的 toolCallId 分开使用。
         */
        static std::string generateMessageId();

    private:
        mutable std::shared_mutex stateMutex; ///< 只在读取或更新内存数据时持锁。
        std::unordered_map<std::string, Session> sessions; ///< 会话 ID 到会话数据的映射。

        /**
         * @brief 组合时间戳、随机标识和递增序号生成 ID。
         * @param prefix 区分会话和消息的前缀。
         * @return 带前缀的 ID；进程内共享序号保证同毫秒、多管理器生成时不重复。
         */
        static std::string generateId(const char *prefix);
    };
} // namespace modelBridge

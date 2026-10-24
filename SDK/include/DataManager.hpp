#pragma once
#include "Common.hpp"
#include <mutex>

struct sqlite3;

namespace modelBridge
{
    /**
     * @brief 使用单连接保存 SQLite 会话数据；公开操作内部加锁。
     * 数据库错误抛出 std::runtime_error，不与无记录或版本过期混淆。
     */
    class DataManager
    {
    public:
        /// 消息提交结果；数据库错误通过异常反馈。
        enum class WriteResult
        {
            Success,
            NotFound,
            VersionMismatch
        };

        /** @brief 打开数据库并建表；路径为空时使用 SDK/DBFiles/modelbridge.db。
         * @param databasePath 数据库路径，支持 SQLite 的 :memory: 路径。
         * 初始化失败抛出异常，已有数据库内容保留。
         */
        DataManager(const std::string &databasePath = "");
        /** @brief 关闭数据库，释放连接资源。 */
        ~DataManager();
        DataManager(const DataManager &) = delete;
        DataManager &operator=(const DataManager &) = delete;

        /** @brief 在一个事务中新增会话及其初始消息，保留已有 ID 和时间。
         * @param session 由 SessionManager 准备的完整会话；重复会话 ID 抛出异常。
         */
        void createSession(const Session &session);

        /** @brief 更新标题、最近模型标识和活动时间，不替换历史或创建时间。
         * @param session 包含待更新字段的会话。@return 不存在返回 false。
         */
        bool updateSession(const Session &session);

        /** @brief 事务内按顺序追加消息、按 ID 去重并更新会话信息。
         * @param sessionId 已存在的会话 ID。
         * @param messages 已补齐 ID、时间和模型信息的消息链。
         * @param updatedAt 成功新增消息时使用的会话活动时间。
         * @param expectedGeneration 请求读取历史时的版本；空值表示不检查。
         * @return 成功、会话不存在或版本过期；空列表和重复记录不更新会话。
         */
        WriteResult appendMessages(const std::string &sessionId, const std::vector<Message> &messages,
                                   std::int64_t updatedAt, std::optional<std::uint64_t> expectedGeneration = std::nullopt);

        /** @brief 追加单条已准备好的消息，规则与 appendMessages 相同。
         * @param sessionId 已存在的会话 ID。@param message 保留原 ID 和元数据的消息。
         * @param updatedAt 新增成功时的活动时间。@param expectedGeneration 可选的版本检查。
         * @return Success、NotFound 或 VersionMismatch。
         */
        WriteResult appendMessage(const std::string &sessionId, const Message &message,
                                  std::int64_t updatedAt, std::optional<std::uint64_t> expectedGeneration = std::nullopt);

        /** @brief 按原顺序读取会话及全部消息。
         * @param sessionId 会话 ID。@return 会话快照；不存在返回 nullopt。
         */
        std::optional<Session> getSession(const std::string &sessionId) const;
        /** @brief 读取完整消息，保留所有协议字段。
         * @param sessionId 会话 ID。@return 消息快照；不存在返回 nullopt。
         */
        std::optional<std::vector<Message>> getMessages(const std::string &sessionId) const;
        /** @brief 查询指定用户的会话概要，不加载消息。
         * @param userId 明确的用户范围。@return 按更新时间降序、ID 升序排列的列表。
         */
        std::vector<Session> getSessions(const std::string &userId) const;

        /** @brief 事务内替换 System 消息及活动时间，不改变其他消息。
         * @param sessionId 会话 ID。
         * @param prompt 带 ID 和时间的 System 消息，nullopt 表示移除。
         * @param updatedAt 新的活动时间。
         * @return 会话不存在返回 false。
         */
        bool setSystemPrompt(const std::string &sessionId, const std::optional<Message> &prompt, std::int64_t updatedAt);
        /** @brief 仅更新活动时间。
         * @param sessionId 会话 ID。@param updatedAt 新的活动时间。@return 不存在返回 false。
         */
        bool updateSessionTimestamp(const std::string &sessionId, std::int64_t updatedAt);
        /** @brief 清除非 System 消息并递增版本。
         * @param sessionId 会话 ID。@param updatedAt 新的活动时间。@return 不存在返回 false。
         */
        bool clearMessages(const std::string &sessionId, std::int64_t updatedAt);
        /** @brief 删除会话及关联消息。
         * @param sessionId 会话 ID。@return 不存在返回 false。
         */
        bool deleteSession(const std::string &sessionId);
        /** @brief 仅删除明确指定用户的全部会话及消息，不影响其他用户。
         * @param userId 用户 ID。@return 无，数据库错误抛出异常。
         */
        void clearSessions(const std::string &userId);

    private:
        sqlite3 *db = nullptr;
        mutable std::mutex databaseMutex; ///< 覆盖一次完整读写或事务。
    };
} // namespace modelBridge

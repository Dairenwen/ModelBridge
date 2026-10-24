#include "DataManager.hpp"
#include "DataManagerSchema.hpp"
#include <sqlite3.h>
#include <json/json.h>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace modelBridge
{
    namespace
    {
        [[noreturn]] void databaseError(sqlite3 *db, const char *operation)
        {
            throw std::runtime_error(std::string(operation) + ": " + sqlite3_errmsg(db));
        }

        void executeSql(sqlite3 *db, const char *sql)
        {
            // sqlite3_exec 不申请错误字符串，统一从连接读取错误，避免遗漏 sqlite3_free。
            if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
                databaseError(db, "SQLite execution failed");
        }

        /// 只封装语句资源和参数绑定，不引入额外的数据访问层。
        class Statement
        {
        public:
            Statement(sqlite3 *connection, const char *sql) : db(connection)
            {
                sqlite3_stmt *prepared = nullptr;
                const int result = sqlite3_prepare_v2(db, sql, -1, &prepared, nullptr);
                statement.reset(prepared);
                if (result != SQLITE_OK)
                    databaseError(db, "SQLite prepare failed");
            }

            void bind(int index, const std::string &value)
            {
                check(sqlite3_bind_text64(statement.get(), index, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
            }
            void bind(int index, std::int64_t value) { check(sqlite3_bind_int64(statement.get(), index, value)); }
            void bind(int index, const std::optional<std::string> &value)
            {
                if (value) bind(index, *value);
                else check(sqlite3_bind_null(statement.get(), index));
            }
            void bind(int index, const std::optional<bool> &value)
            {
                if (value.has_value()) bind(index, static_cast<std::int64_t>(*value));
                else check(sqlite3_bind_null(statement.get(), index));
            }
            bool step()
            {
                const int result = sqlite3_step(statement.get());
                if (result == SQLITE_ROW) return true;
                if (result == SQLITE_DONE) return false;
                databaseError(db, "SQLite step failed");
            }
            void reset()
            {
                check(sqlite3_reset(statement.get()));
                check(sqlite3_clear_bindings(statement.get()));
            }
            std::string text(int column) const
            {
                const auto *value = sqlite3_column_text(statement.get(), column);
                return value ? std::string(reinterpret_cast<const char *>(value), sqlite3_column_bytes(statement.get(), column)) : std::string{};
            }
            std::int64_t integer(int column) const { return sqlite3_column_int64(statement.get(), column); }
            bool isNull(int column) const { return sqlite3_column_type(statement.get(), column) == SQLITE_NULL; }

        private:
            void check(int result)
            {
                if (result != SQLITE_OK) databaseError(db, "SQLite binding/reset failed");
            }
            sqlite3 *db;
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{nullptr, sqlite3_finalize};
        };

        /// 一个事务的所有语句均由调用方持有 databaseMutex；失败时自动回滚。
        class Transaction
        {
        public:
            explicit Transaction(sqlite3 *connection, bool write = true) : db(connection)
            {
                executeSql(db, write ? "BEGIN IMMEDIATE" : "BEGIN");
            }
            ~Transaction()
            {
                if (!committed) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            }
            void commit()
            {
                executeSql(db, "COMMIT");
                committed = true;
            }
        private:
            sqlite3 *db;
            bool committed = false;
        };

        std::int64_t sqlGeneration(std::uint64_t value)
        {
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                throw std::runtime_error("Session generation exceeds SQLite INTEGER range");
            return static_cast<std::int64_t>(value);
        }

        std::string encodeToolCalls(const std::vector<ToolCall> &calls)
        {
            Json::Value values(Json::arrayValue);
            for (const auto &call : calls)
            {
                Json::Value value;
                value["id"] = call.id;
                value["name"] = call.name;
                value["arguments"] = call.arguments;
                values.append(std::move(value));
            }
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            return Json::writeString(writer, values);
        }

        std::vector<ToolCall> decodeToolCalls(const std::string &text)
        {
            Json::CharReaderBuilder builder;
            builder["failIfExtra"] = true;
            std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            Json::Value values;
            std::string error;
            if (!reader->parse(text.data(), text.data() + text.size(), &values, &error) || !values.isArray())
                throw std::runtime_error("Invalid tool calls stored in SQLite");
            std::vector<ToolCall> calls;
            calls.reserve(values.size());
            for (const auto &value : values)
            {
                if (!value.isObject() || !value["id"].isString() || !value["name"].isString() || !value["arguments"].isString())
                    throw std::runtime_error("Invalid tool call fields stored in SQLite");
                calls.push_back({value["id"].asString(), value["name"].asString(), value["arguments"].asString()});
            }
            return calls;
        }

        Session readSession(const Statement &row)
        {
            Session session{};
            session.sessionId = row.text(0);
            session.userId = row.text(1);
            session.modelConfigId = row.text(2);
            session.title = row.text(3);
            session.createdAt = row.integer(4);
            session.updatedAt = row.integer(5);
            session.generation = static_cast<std::uint64_t>(row.integer(6));
            return session;
        }

        Message readMessage(const Statement &row)
        {
            Message message{};
            message.messageId = row.text(0);
            message.role = static_cast<MessageRole>(row.integer(1));
            message.content = row.text(2);
            message.createdAt = row.integer(3);
            message.toolCallId = row.text(4);
            message.toolName = row.text(5);
            message.toolCalls = decodeToolCalls(row.text(6));
            message.reasoningContent = row.text(7);
            message.protocolData = row.text(8);
            message.modelConfigId = row.text(9);
            message.modelName = row.text(10);
            if (!row.isNull(11)) message.reasoningEffort = row.text(11);
            if (!row.isNull(12)) message.thinkingEnabled = row.integer(12) != 0;
            return message;
        }

        constexpr const char *insertMessageSql =
            "INSERT INTO messages(sessionId,messageId,position,role,content,createdAt,toolCallId,toolName,"
            "toolCalls,reasoningContent,protocolData,modelConfigId,modelName,reasoningEffort,thinkingEnabled) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(sessionId,messageId) DO NOTHING";

        bool insertMessage(sqlite3 *db, Statement &statement, const std::string &sessionId,
                           const Message &message, std::int64_t position)
        {
            if (message.messageId.empty())
                throw std::invalid_argument("Message ID must be assigned before persistence");
            statement.bind(1, sessionId);
            statement.bind(2, message.messageId);
            statement.bind(3, position);
            statement.bind(4, static_cast<std::int64_t>(message.role));
            statement.bind(5, message.content);
            statement.bind(6, message.createdAt);
            statement.bind(7, message.toolCallId);
            statement.bind(8, message.toolName);
            statement.bind(9, encodeToolCalls(message.toolCalls));
            statement.bind(10, message.reasoningContent);
            statement.bind(11, message.protocolData);
            statement.bind(12, message.modelConfigId);
            statement.bind(13, message.modelName);
            statement.bind(14, message.reasoningEffort);
            statement.bind(15, message.thinkingEnabled);
            statement.step();
            const bool inserted = sqlite3_changes(db) != 0;
            statement.reset();
            return inserted;
        }

        /// 在已有事务中检查记录及版本，不重复加锁或创建事务。
        DataManager::WriteResult checkSession(sqlite3 *db, const std::string &id,
                                             std::optional<std::uint64_t> expected = std::nullopt)
        {
            Statement query(db, "SELECT generation FROM sessions WHERE sessionId=?");
            query.bind(1, id);
            if (!query.step()) return DataManager::WriteResult::NotFound;
            if (expected && query.integer(0) != sqlGeneration(*expected))
                return DataManager::WriteResult::VersionMismatch;
            return DataManager::WriteResult::Success;
        }
    } // namespace

    DataManager::DataManager(const std::string &databasePath)
    {
        const std::filesystem::path path(databasePath.empty() ? detail::defaultDatabasePath : databasePath);
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        const int result = sqlite3_open(path.string().c_str(), &db);
        try
        {
            if (result != SQLITE_OK)
                throw std::runtime_error(std::string("Cannot open SQLite database: ") + (db ? sqlite3_errmsg(db) : "out of memory"));
            if (sqlite3_busy_timeout(db, 5000) != SQLITE_OK)
                databaseError(db, "Cannot configure SQLite busy timeout");
            executeSql(db, "PRAGMA foreign_keys = ON");
            Transaction transaction(db);
            executeSql(db, detail::databaseSchema);
            transaction.commit();
        }
        catch (...)
        {
            if (db) sqlite3_close(db);
            db = nullptr;
            throw;
        }
    }

    DataManager::~DataManager()
    {
        if (db) sqlite3_close(db);
    }

    void DataManager::createSession(const Session &session)
    {
        if (session.sessionId.empty()) throw std::invalid_argument("Session ID must be assigned before persistence");
        std::lock_guard<std::mutex> lock(databaseMutex);
        Transaction transaction(db);
        Statement insert(db, "INSERT INTO sessions(sessionId,userId,modelConfigId,title,createdAt,updatedAt,generation) VALUES(?,?,?,?,?,?,?)");
        insert.bind(1, session.sessionId);
        insert.bind(2, session.userId);
        insert.bind(3, session.modelConfigId);
        insert.bind(4, session.title);
        insert.bind(5, session.createdAt);
        insert.bind(6, session.updatedAt);
        insert.bind(7, sqlGeneration(session.generation));
        insert.step();
        Statement messages(db, insertMessageSql);
        std::int64_t position = 0;
        for (const auto &message : session.messages)
            if (insertMessage(db, messages, session.sessionId, message, position)) ++position;
        transaction.commit();
    }

    bool DataManager::updateSession(const Session &session)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Statement update(db, "UPDATE sessions SET title=?,modelConfigId=?,updatedAt=? WHERE sessionId=?");
        update.bind(1, session.title);
        update.bind(2, session.modelConfigId);
        update.bind(3, session.updatedAt);
        update.bind(4, session.sessionId);
        update.step();
        return sqlite3_changes(db) != 0;
    }

    DataManager::WriteResult DataManager::appendMessage(const std::string &sessionId, const Message &message,
                                                        std::int64_t updatedAt, std::optional<std::uint64_t> expectedGeneration)
    {
        return appendMessages(sessionId, {message}, updatedAt, expectedGeneration);
    }

    DataManager::WriteResult DataManager::appendMessages(const std::string &sessionId, const std::vector<Message> &messages,
                                                         std::int64_t updatedAt, std::optional<std::uint64_t> expectedGeneration)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Transaction transaction(db);
        const auto result = checkSession(db, sessionId, expectedGeneration);
        if (result != WriteResult::Success) return result;
        Statement last(db, "SELECT COALESCE(MAX(position),-1) FROM messages WHERE sessionId=?");
        last.bind(1, sessionId);
        last.step();
        auto position = last.integer(0);
        Statement insert(db, insertMessageSql);
        const Message *lastInserted = nullptr;
        for (const auto &message : messages)
        {
            if (position == std::numeric_limits<std::int64_t>::max())
                throw std::runtime_error("Message position exceeds SQLite INTEGER range");
            if (insertMessage(db, insert, sessionId, message, position + 1))
            {
                ++position;
                lastInserted = &message;
            }
        }
        if (lastInserted)
        {
            Statement update(db, "UPDATE sessions SET modelConfigId=?,updatedAt=? WHERE sessionId=?");
            update.bind(1, lastInserted->modelConfigId);
            update.bind(2, updatedAt);
            update.bind(3, sessionId);
            update.step();
        }
        transaction.commit();
        return WriteResult::Success;
    }

    std::optional<Session> DataManager::getSession(const std::string &sessionId) const
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        // 会话元数据和消息在同一读取快照中，避免其他连接在两次查询之间清空消息。
        Transaction transaction(db, false);
        Statement query(db, "SELECT sessionId,userId,modelConfigId,title,createdAt,updatedAt,generation FROM sessions WHERE sessionId=?");
        query.bind(1, sessionId);
        if (!query.step()) return std::nullopt;
        auto session = readSession(query);
        Statement messages(db, "SELECT messageId,role,content,createdAt,toolCallId,toolName,toolCalls,reasoningContent,"
                               "protocolData,modelConfigId,modelName,reasoningEffort,thinkingEnabled "
                               "FROM messages WHERE sessionId=? ORDER BY position");
        messages.bind(1, sessionId);
        while (messages.step()) session.messages.push_back(readMessage(messages));
        transaction.commit();
        return session;
    }

    std::optional<std::vector<Message>> DataManager::getMessages(const std::string &sessionId) const
    {
        auto session = getSession(sessionId);
        if (!session) return std::nullopt;
        return std::move(session->messages);
    }

    std::vector<Session> DataManager::getSessions(const std::string &userId) const
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Statement query(db, "SELECT sessionId,userId,modelConfigId,title,createdAt,updatedAt,generation "
                            "FROM sessions WHERE userId=? ORDER BY updatedAt DESC,sessionId");
        query.bind(1, userId);
        std::vector<Session> sessions;
        while (query.step()) sessions.push_back(readSession(query));
        return sessions;
    }

    bool DataManager::setSystemPrompt(const std::string &sessionId, const std::optional<Message> &prompt, std::int64_t updatedAt)
    {
        if (prompt && prompt->role != MessageRole::System)
            throw std::invalid_argument("System prompt must use the System message role");
        std::lock_guard<std::mutex> lock(databaseMutex);
        Transaction transaction(db);
        if (checkSession(db, sessionId) == WriteResult::NotFound) return false;
        Statement remove(db, "DELETE FROM messages WHERE sessionId=? AND role=?");
        remove.bind(1, sessionId);
        remove.bind(2, static_cast<std::int64_t>(MessageRole::System));
        remove.step();
        if (prompt)
        {
            Statement first(db, "SELECT COALESCE(MIN(position),1) FROM messages WHERE sessionId=?");
            first.bind(1, sessionId);
            first.step();
            auto position = first.integer(0);
            if (position == std::numeric_limits<std::int64_t>::min())
                throw std::runtime_error("System message position exceeds SQLite INTEGER range");
            Statement insert(db, insertMessageSql);
            if (!insertMessage(db, insert, sessionId, *prompt, position - 1))
                throw std::invalid_argument("System message ID conflicts with an existing message");
        }
        Statement update(db, "UPDATE sessions SET updatedAt=? WHERE sessionId=?");
        update.bind(1, updatedAt);
        update.bind(2, sessionId);
        update.step();
        transaction.commit();
        return true;
    }

    bool DataManager::updateSessionTimestamp(const std::string &sessionId, std::int64_t updatedAt)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Statement update(db, "UPDATE sessions SET updatedAt=? WHERE sessionId=?");
        update.bind(1, updatedAt);
        update.bind(2, sessionId);
        update.step();
        return sqlite3_changes(db) != 0;
    }

    bool DataManager::clearMessages(const std::string &sessionId, std::int64_t updatedAt)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Transaction transaction(db);
        if (checkSession(db, sessionId) == WriteResult::NotFound) return false;
        Statement remove(db, "DELETE FROM messages WHERE sessionId=? AND role<>?");
        remove.bind(1, sessionId);
        remove.bind(2, static_cast<std::int64_t>(MessageRole::System));
        remove.step();
        Statement update(db, "UPDATE sessions SET generation=generation+1,updatedAt=? WHERE sessionId=? AND generation<?");
        update.bind(1, updatedAt);
        update.bind(2, sessionId);
        update.bind(3, std::numeric_limits<std::int64_t>::max());
        update.step();
        if (!sqlite3_changes(db)) throw std::runtime_error("Session generation exceeds SQLite INTEGER range");
        transaction.commit();
        return true;
    }

    bool DataManager::deleteSession(const std::string &sessionId)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Statement remove(db, "DELETE FROM sessions WHERE sessionId=?");
        remove.bind(1, sessionId);
        remove.step();
        return sqlite3_changes(db) != 0;
    }

    void DataManager::clearSessions(const std::string &userId)
    {
        std::lock_guard<std::mutex> lock(databaseMutex);
        Statement remove(db, "DELETE FROM sessions WHERE userId=?");
        remove.bind(1, userId);
        remove.step();
    }
} // namespace modelBridge

-- 默认文件：SDK/DBFiles/modelbridge.db；也可由调用方传入其他路径。
-- 外键由连接初始化时的 PRAGMA foreign_keys = ON 启用。

-- 会话元数据；generation 在清空聊天消息时递增。
CREATE TABLE IF NOT EXISTS sessions (
    sessionId TEXT NOT NULL PRIMARY KEY,
    userId TEXT NOT NULL,
    modelConfigId TEXT NOT NULL,
    title TEXT NOT NULL,
    createdAt INTEGER NOT NULL,
    updatedAt INTEGER NOT NULL,
    generation INTEGER NOT NULL CHECK (generation >= 0)
);

-- position 仅用于数据库排序，不改变消息 ID 或创建时间。可为负数，便于替换系统提示时将新 System 消息放在现有历史前面。
CREATE TABLE IF NOT EXISTS messages (
    sessionId TEXT NOT NULL REFERENCES sessions(sessionId) ON DELETE CASCADE,
    messageId TEXT NOT NULL,
    position INTEGER NOT NULL,
    role INTEGER NOT NULL CHECK (role BETWEEN 0 AND 3),
    content TEXT NOT NULL,
    createdAt INTEGER NOT NULL,
    toolCallId TEXT NOT NULL,
    toolName TEXT NOT NULL,
    toolCalls TEXT NOT NULL,
    reasoningContent TEXT NOT NULL,
    protocolData TEXT NOT NULL,
    modelConfigId TEXT NOT NULL,
    modelName TEXT NOT NULL,
    reasoningEffort TEXT,
    thinkingEnabled INTEGER CHECK (thinkingEnabled IN (0, 1)),
    PRIMARY KEY (sessionId, messageId),
    UNIQUE (sessionId, position)
);

-- 消息唯一约束同时建立按会话与顺序查询所需的索引。列表查询只读取会话表，不加载消息正文。
CREATE INDEX IF NOT EXISTS sessions_user_updated ON sessions(userId, updatedAt DESC, sessionId);

#pragma once
#include "Common.hpp"
#include <memory>
#include <shared_mutex>

namespace Json
{
    class Value;
}

namespace modelBridge
{
    class SessionManager;
    /**
     * @brief 接收流式回复；所有片段按顺序拼接后得到完整文本。
     * @param delta 本次新增文本，可为空；引用仅在回调期间有效。
     * @param done 是否为本次回复的最后一次回调；先处理 delta，再处理结束标记。
     * @return 无。
     */
    using StreamCallback = std::function<void(const std::string &delta, bool done)>;

    /**
     * @brief 统一的文本聊天接口；三个子类分别处理不同协议。
     * 初始化只校验本地配置，不发送计费请求，也不代表远端密钥已通过验证。
     * 同一对象可并发发送，每次调用使用开始时的配置、工具和历史快照。
     * 本轮已完成的消息链按提交顺序整体追加，不会与另一轮的工具消息交错。
     * 并发请求不会看到彼此尚未完成的回复；有上下文先后依赖时需等待前一轮完成。
     * 绑定会话的在途请求始终保存到原会话，清空或删除该会话后不再写回。
     * 本地历史在重新初始化或切换绑定后失效，在途请求仍可返回结果。
     * 工具和回调在锁外执行，其捕获的外部共享数据由调用方同步；发送失败抛出异常。
     * 调用期间入参须保持有效且不被其他线程修改；对象销毁前须等待所有调用结束。
     */
    class LLMProvider
    {
    public:
        virtual ~LLMProvider() = default;

        /**
         * @brief 初始化连接配置，优先使用 apiKey，否则读取 apiKeyEnv。
         * @param modelConfig 模型名称、协议、服务根地址和生成参数。
         * @return 配置有效且认证信息齐全时返回 true；失败原因见 getLastError()。
         */
        bool initialize(const ModelConfig &modelConfig);

        /** @brief 查询最近一次初始化是否成功。 */
        bool isInitialized() const;

        /** @brief 获取模型名称。@return 配置中的模型 API 标识。 */
        std::string getModelName() const;

        /** @brief 获取模型描述。@return description，可为空。 */
        std::string getModelDescription() const;

        /** @brief 获取模型信息。@return 当前模型信息的副本。 */
        ModelInfo getModelInfo() const;

        /** @brief 获取初始化失败原因。@return 最近一次初始化错误，成功时为空。 */
        std::string getLastError() const;

        /**
         * @brief 加锁更新当前密钥，供后续请求使用，不重新校验配置或改变初始化状态。
         * 初始化失败后，须将有效密钥放入 ModelConfig 并重新调用 initialize()。
         * @param apiKey 新密钥
         * @return 无
         */
        void setApiKey(const std::string &apiKey);

        /** @brief 获取当前密钥。@return 实际使用的密钥；调用方不可将其写入日志。 */
        std::string getApiKey() const;

        /** @brief 设置后续请求使用的系统提示词，空字符串表示不使用。 */
        void setSystemPrompt(const std::string &prompt);

        /** @brief 清空已完成的对话历史，保留系统提示词。 */
        void clearHistory();

        /** @brief 获取 User/Assistant/Tool 历史快照，不包含系统提示词。 */
        std::vector<Message> getHistory() const;

        /** @brief 绑定已有会话，自动读取及保存历史；不存在返回 false。
         * 使用会话中的系统提示；nullptr 解除绑定并清空本地历史。
         * 切换绑定后，在途请求仍保存到原会话；初始化保留绑定会话的历史。
         */
        bool bindSession(std::shared_ptr<SessionManager> manager, const std::string &sessionId = "");

        /** @brief 注册工具声明及实现；空列表表示关闭工具调用。 */
        void setTools(const std::vector<Tool> &value);

        /**
         * @brief 自动携带历史，发送当前消息并等待一次性回复。
         * @param message 当前用户消息。
         * @return content 为本次全部可见文本，messages 为本次 User/Assistant/Tool 消息链。
         * 后续请求失败时，已经执行的工具结果仍保留。
         */
        virtual ChatResult sendMessage(const std::string &message);

        /**
         * @brief 自动携带历史并接收文本增量；本方法阻塞，回调在调用线程执行。
         * @param message 当前用户消息。
         * @param callback 增量回调，不能为空；保存本轮问答后调用一次 ("", true)。
         * @return content 为增量拼接文本，messages 为本次完整消息链。失败时不保存未完成的响应，已完成工具步骤仍保留。
         * 结束回调抛出异常时，本轮历史已经保存，不回滚。
         */
        virtual ChatResult sendMessageStream(const std::string &message, const StreamCallback &callback);

        /** @brief 获取子类使用的协议。@return 对应的 ApiProtocol。 */
        virtual ApiProtocol getProtocol() const = 0;

    protected:
        /// 单次发送独占的状态；协议回退也只改变本次请求的解析方式。
        struct RequestContext
        {
            std::shared_ptr<SessionManager> sessionManager; ///< 本轮绑定的管理器，保持其生命周期。
            std::string sessionId; ///< 本轮绑定的会话。
            std::uint64_t sessionGeneration = 0; ///< 防止其他 Provider 清空会话后旧请求回写。
            ModelConfig config{};                ///< 开始发送时的连接配置。
            std::vector<Tool> tools;             ///< 本轮可执行的工具副本。
            bool initialized = false;           ///< 开始发送时的初始化状态。
            bool responses = false;             ///< 本轮是否使用 Responses 协议。
            std::uint64_t historyGeneration = 0; ///< 本轮历史快照所属的清空版本。
        };

        /**
         * @brief 构造协议请求体。
         * @param messages 历史。
         * @param stream 是否流式。
         * @param context 本次调用的配置、工具和协议快照。
         * @return JSON 对象。
         */
        virtual Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const = 0;

        /**
         * @brief 构造协议路径。
         * @param stream 是否流式。
         * @param context 本次调用的配置和协议快照。
         * @return 相对 baseUrl 的路径。
         */
        virtual std::string makeRequestPath(bool stream, const RequestContext &context) const = 0;

        /**
         * @brief 从完整响应或一个流事件中读取文本，遇到服务端错误时抛出异常。
         * @param payload 已解析的 JSON。
         * @param stream 是否流事件。
         * @param reply 当前 HTTP 响应中累积的工具调用、思考内容和原生元数据。
         * @param finished 输出协议是否明确结束；OpenAI 的 [DONE] 在公共层处理。
         * @param context 本次调用的配置和协议快照。
         * @return 新增文本，不包含模型的思考过程。
         */
        virtual std::string readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished, const RequestContext &context) const = 0;

        // 只有 OpenAI 兼容协议需要 Chat -> Responses 的单次回退。
        virtual bool tryProtocolFallback(const Json::Value &, RequestContext &) { return false; }
        // 以下三个缓存钩子由基类持有状态锁时调用，子类无需重复加锁。
        virtual void captureProtocol(RequestContext &) const {}
        virtual void rememberProtocol(const RequestContext &) {}
        virtual void resetProtocol() {}
        virtual bool requiresDoneMarker(const RequestContext &) const { return false; }

    private:
        std::shared_ptr<SessionManager> sessionManager;
        std::string sessionId;
        mutable std::shared_mutex stateMutex; ///< 保护共享状态，不覆盖网络、工具和用户回调。
        ModelConfig config{};                 ///< 当前连接配置。
        std::vector<Tool> tools;              ///< 当前注册的工具。
        bool configurationValid = false; ///< 模型、地址及参数是否通过本地校验。
        std::string lastError;           ///< 初始化错误
        std::string systemPrompt;        ///< 系统提示词，发送时放在消息列表最前面。
        std::vector<Message> history;    ///< 已收到的模型消息和已执行的工具结果。
        std::uint64_t historyGeneration = 0; ///< 清空或初始化时递增，阻止旧请求恢复已清空的历史。

        /** @brief 持有状态锁时校验配置；失败抛出异常。 */
        void validateConfiguration();

        /** @brief 在一次读锁内获取状态快照并构造上下文，不修改已保存的历史。 */
        std::vector<Message> buildMessages(const std::string &message, RequestContext &context) const;

        /** @brief 统一处理本轮请求、工具执行及历史保存，直到模型不再请求工具。 */
        ChatResult performRequest(const std::string &message, bool stream, const StreamCallback &callback);

        /**
         * @brief 发送单轮 HTTP 请求，处理协议回退并解析回复。
         * @param messages 历史消息。
         * @param stream 流式开关。
         * @param callback 增量回调。
         * @param context 本轮独占状态，协议回退不会改变其他在途请求。
         * @return 完整 Assistant 消息。
         */
        Message requestOnce(const std::vector<Message> &messages, bool stream, const StreamCallback &callback, RequestContext &context);
        /** @brief 追加到本轮原会话或本地历史；清空历史前启动的请求不再提交。 */
        void saveHistory(const std::vector<Message> &messages, const RequestContext &context);
    };

    /// 优先 Chat Completions；明确不支持协议时回退 Responses，并在本次初始化期间沿用。
    class OpenAICompatibleProvider : public LLMProvider
    {
    public:
        /**
         * @brief 获取协议。* @return OpenAICompatible。
         */
        ApiProtocol getProtocol() const override;

    protected:
        /// 构造 messages 及顶层生成参数，参数和返回值含义同基类。
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const override;
        /// 返回 /chat/completions 或回退后的 /responses，stream 不改变路径。
        std::string makeRequestPath(bool stream, const RequestContext &context) const override;
        /// 读取 Chat 文本/工具增量或 Responses 事件，其他参数含义同基类。
        std::string readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished, const RequestContext &context) const override;
        bool tryProtocolFallback(const Json::Value &error, RequestContext &context) override;
        void captureProtocol(RequestContext &context) const override;
        void rememberProtocol(const RequestContext &context) override;
        void resetProtocol() override;
        bool requiresDoneMarker(const RequestContext &context) const override;

    private:
        bool responses = false; // 先尝试Chat Completions，如果请求失败会调用response
    };

    /// Gemini 原生 generateContent / streamGenerateContent 协议。
    class GeminiProvider : public LLMProvider
    {
    public:
        /** @brief 获取协议。@return Gemini。
         */
        ApiProtocol getProtocol() const override;

    protected:
        /// 将系统消息映射到 systemInstruction，将 Assistant 映射到 model。
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const override;
        /// 根据 stream 选择普通或 SSE 路径，模型标识会进行 URL 编码。
        std::string makeRequestPath(bool stream, const RequestContext &context) const override;
        /// 读取首个候选的文本 parts，忽略 thought 内容并检查拦截结果。
        std::string readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished, const RequestContext &context) const override;
    };

    /// Ollama 原生 /api/chat 协议，支持无认证的本地服务。
    class OllamaProvider : public LLMProvider
    {
    public:
        /** @brief 获取协议。@return Ollama。 */
        ApiProtocol getProtocol() const override;

    protected:
        /// 构造 messages 和 options，参数及返回值含义同基类。
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream, const RequestContext &context) const override;
        /// 返回 /api/chat；baseUrl 已以 /api 结尾时只追加 /chat。
        std::string makeRequestPath(bool stream, const RequestContext &context) const override;
        /// 读取 message.content 和 done，参数及返回值含义同基类。
        std::string readResponse(const Json::Value &payload, bool stream, Message &reply, bool &finished, const RequestContext &context) const override;
    };

}

#pragma once
#include "Common.hpp"
#include <memory>

namespace Json
{
    class Value;
}

namespace modelBridge
{
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
     * 一个对象管理一段对话，请按顺序调用；发送失败抛出异常。
     * 发送期间（包括增量回调中）不要再次发送，或修改配置、密钥及对话状态。
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
        const std::string &getModelName() const;

        /** @brief 获取模型描述。@return description，可为空。 */
        const std::string &getModelDescription() const;

        /** @brief 获取模型信息。@return 当前模型信息的只读引用。 */
        const ModelInfo &getModelInfo() const;

        /** @brief 获取初始化失败原因。@return 最近一次初始化错误，成功时为空。 */
        const std::string &getLastError() const;

        /**
         * @brief 更新当前密钥，之前仅因缺少密钥而未就绪的连接可立即使用。
         * @param apiKey 新密钥
         * @return 无
         */
        void setApiKey(const std::string &apiKey);

        /** @brief 获取当前密钥。@return 实际使用的密钥；调用方不可将其写入日志。 */
        const std::string &getApiKey() const;

        /** @brief 设置后续请求使用的系统提示词，空字符串表示不使用。 */
        void setSystemPrompt(const std::string &prompt);

        /** @brief 清空已完成的对话历史，保留系统提示词。 */
        void clearHistory();

        /** @brief 获取已完成的 User/Assistant 消息，不包含系统提示词；不要跨后续修改持有其中元素的引用。 */
        const std::vector<Message> &getHistory() const;

        /**
         * @brief 自动携带历史，发送当前消息并等待一次性回复。
         * @param message 当前用户消息。
         * @return 完整回复文本；成功后保存本轮问答，请求失败则保留原历史。
         */
        virtual std::string sendMessage(const std::string &message);

        /**
         * @brief 自动携带历史并接收文本增量；本方法阻塞，回调在调用线程执行。
         * @param message 当前用户消息。
         * @param callback 增量回调，不能为空；保存本轮问答后调用一次 ("", true)。
         * @return 拼接后的完整文本；请求或增量回调失败时不保存本轮问答。
         * 结束回调抛出异常时，本轮历史已经保存，不回滚。
         */
        virtual std::string sendMessageStream(const std::string &message, const StreamCallback &callback);

        /** @brief 获取子类使用的协议。@return 对应的 ApiProtocol。 */
        virtual ApiProtocol getProtocol() const = 0;

    protected:
        ModelConfig config{}; ///< 当前连接配置；只在初始化或显式设置密钥时修改。

        /**
         * @brief 构造协议请求体。
         * @param messages 历史。
         * @param stream 是否流式。
         * @return JSON 对象。
         */
        virtual Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream) const = 0;

        /**
         * @brief 构造协议路径。
         * @param stream 是否流式。
         * @return 相对 baseUrl 的路径。
         */
        virtual std::string makeRequestPath(bool stream) const = 0;

        /**
         * @brief 从完整响应或一个流事件中读取文本，遇到服务端错误时抛出异常。
         * @param payload 已解析的 JSON。
         * @param stream 是否流事件。
         * @param finished 输出协议是否明确结束；OpenAI 的 [DONE] 在公共层处理。
         * @return 新增文本，不包含模型的思考过程。
         */
        virtual std::string readResponse(const Json::Value &payload, bool stream, bool &finished) const = 0;

    private:
        bool configurationValid = false; ///< 模型、地址及参数是否通过本地校验。
        std::string lastError;           ///< 初始化错误
        std::string systemPrompt;        ///< 系统提示词，发送时放在消息列表最前面。
        std::vector<Message> history;    ///< 仅保存已完成的 User/Assistant 消息。

        /** @brief 用系统提示词、历史副本和当前用户消息构造请求，不修改已保存的历史。 */
        std::vector<Message> buildMessages(const std::string &message) const;

        /**
         * @brief 执行公共 HTTP 流程
         * @param messages 历史消息。
         * @param stream 流式开关。
         * @param callback 增量回调。
         * @return 回复文本。
         */
        std::string performRequest(const std::vector<Message> &messages, bool stream, const StreamCallback &callback);
    };

    /// OpenAI Chat Completions 兼容协议，也可用于百炼、DeepSeek 和兼容的中转服务
    class OpenAICompatibleProvider : public LLMProvider
    {
    public:
        /**
         * @brief 获取协议。* @return OpenAICompatible。
         */
        ApiProtocol getProtocol() const override;

    protected:
        /// 构造 messages 及顶层生成参数，参数和返回值含义同基类。
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream) const override;
        /// 返回 /chat/completions，stream 不改变路径。
        std::string makeRequestPath(bool stream) const override;
        /// 读取 choices[0].message 或 choices[0].delta，其他参数含义同基类。
        std::string readResponse(const Json::Value &payload, bool stream, bool &finished) const override;
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
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream) const override;
        /// 根据 stream 选择普通或 SSE 路径，模型标识会进行 URL 编码。
        std::string makeRequestPath(bool stream) const override;
        /// 读取首个候选的文本 parts，忽略 thought 内容并检查拦截结果。
        std::string readResponse(const Json::Value &payload, bool stream, bool &finished) const override;
    };

    /// Ollama 原生 /api/chat 协议，支持无认证的本地服务。
    class OllamaProvider : public LLMProvider
    {
    public:
        /** @brief 获取协议。@return Ollama。 */
        ApiProtocol getProtocol() const override;

    protected:
        /// 构造 messages 和 options，参数及返回值含义同基类。
        Json::Value makeRequestBody(const std::vector<Message> &messages, bool stream) const override;
        /// 返回 /api/chat；baseUrl 已以 /api 结尾时只追加 /chat。
        std::string makeRequestPath(bool stream) const override;
        /// 读取 message.content 和 done，参数及返回值含义同基类。
        std::string readResponse(const Json::Value &payload, bool stream, bool &finished) const override;
    };

}

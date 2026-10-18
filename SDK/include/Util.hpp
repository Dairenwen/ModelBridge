#pragma once
#include <memory>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace modelBridge
{

    /// 共用的异步终端日志，默认输出 WARN 及以上级别。
    class Logger
    {
    public:
        /**
         * @brief 获取 SDK 共用的异步日志对象，首次调用时自动创建。
         * @return 日志对象的引用，可用于调整 spdlog 的其他设置。
         */
        static spdlog::logger &getLogger()
        {
            // 函数内静态变量保证跨源文件共用，且首次初始化是线程安全的。
            static auto logger = []
            {
                // 先初始化输出端的内部锁，再创建线程池，确保退出时锁仍可用于输出。
                auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                // 独立队列容纳 8192 条消息，单个后台线程按入队顺序输出。线程池先于日志对象创建，正常退出时等待队列中的日志处理完成。
                static auto threadPool = std::make_shared<spdlog::details::thread_pool>(8192, 1);
                // 队列满时等待空位，不丢弃日志；实际 stdout 输出由后台线程执行。
                auto instance = std::make_shared<spdlog::async_logger>("modelBridge", sink, threadPool, spdlog::async_overflow_policy::block);
                instance->set_level(spdlog::level::warn);
                instance->set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] [%s:%#] %v");
                instance->flush_on(spdlog::level::warn); // 刷新也在后台线程执行
                return instance;
            }();
            return *logger;
        }

        /**
         * @brief 设置最低输出级别，例如 spdlog::level::debug 或 spdlog::level::off。
         * @param level 最低输出级别，低于它的日志不输出。
         * @return 无。
         */
        static void setLevel(spdlog::level::level_enum level)
        {
            getLogger().set_level(level);
        }
    };

} // namespace modelBridge

/// 输出 TRACE 级别日志，默认不打印。
#define MB_LOG_TRACE(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::trace, __VA_ARGS__)
/// 输出 DEBUG 级别日志，默认不打印。
#define MB_LOG_DEBUG(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::debug, __VA_ARGS__)
/// 输出 INFO 级别日志，默认不打印。
#define MB_LOG_INFO(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::info, __VA_ARGS__)
/// 输出 WARN 级别日志。
#define MB_LOG_WARN(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::warn, __VA_ARGS__)
/// 输出 ERROR 级别日志。
#define MB_LOG_ERROR(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::err, __VA_ARGS__)
/// 输出 CRITICAL 级别日志。
#define MB_LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(&::modelBridge::Logger::getLogger(), ::spdlog::level::critical, __VA_ARGS__)

#ifndef WEBSERVER_LOG_H
#define WEBSERVER_LOG_H

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include "buffer.h"
#include "block_queue.h"

enum class Level {
    DEBUG,      // 调试信息，用于开发阶段详细追踪程序运行流
    INFO,       // 常规信息，记录系统正常运行的关键节点状态
    WARNING,    // 警告信息，系统出现潜在问题但不影响整体运行
    ERROR,      // 错误信息，系统发生严重故障或业务逻辑失败
};

class Log {
private:
    static constexpr int LOG_PATH_LEN = 256;            // 日志路径最大长度
    static constexpr int LOG_NAME_LEN = 256;            // 日志文件最大长度
    static constexpr int MAX_LINES = 50000;             // 单个日志文件最大行数

    static constexpr const char* PATH = "../log_data";  // 默认的日志存储目录
    static constexpr const char* SUFFIX = ".log";       // 默认的日志文件后缀名

    int line_count;                                     // 当前打开的日志文件已经写入的行数
    int today;                                          // 当前日志文件对应的日期（按天），用于判断是否需要跨天轮转文件
    bool is_running;                                    // 日志系统是否正在运行（已成功初始化并打开文件）

    Buffer buffer;                                      // 自定义的自动扩容缓冲区，用于高效、安全地格式化单条日志字符串
    Level level;                                        // 系统的全局日志级别（低于此级别的日志将被忽略）
    bool is_async;                                      // 是否开启异步模式（取决于初始化时设定的队列容量）

    FILE* fp;                                           // 底层 C 标准库文件指针，负责实际的磁盘 I/O

    std::unique_ptr<BlockQueue> queue;                  // 线程安全的阻塞队列，作为生产者（业务线程）和消费者（写线程）的桥梁
    std::unique_ptr<std::thread> write_thread;          // 后台异步写盘线程
    std::mutex mtx;                                     // 全局互斥锁，保护文件指针 fp、buffer 及内部状态变量的并发安全

    // 私有化构造与析构函数，确保单例模式
    Log();
    ~Log();

    // 将当前的日志级别格式化为字符串追加到 buffer 中
    void append_log_level_title(Level _level);

    // 异步写线程的核心循环函数
    // 不断从阻塞队列中取出日志字符串，并真正写入到磁盘文件中
    void async_write();
public:
    // 获取日志单例（C++11 局部静态变量保证线程安全）
    static Log* instance();

    // 初始化参数：日志级别，最大队列容量
    void init(Level _level, size_t _max_queue_capacity = 4096);

    // 核心写日志函数
    void write(Level _level, const char* _format, ...);

    // 强制将 C 标准库缓冲区的日志刷新到内核
    void flush();

    // 获取当前日志级别
    Level get_level() const noexcept { return level; }

    // 检查日志系统是否已经正常运行
    bool running() const noexcept { return is_running; }
};

// 便捷使用的宏定义 (只在等级符合要求时才执行真正地解析，节省性能)
#define LOG_BASE(level, format, ...) \
    do { \
        Log* log = Log::instance(); \
        if (log->running() && log->get_level() <= level) { \
            log->write(level, format, ##__VA_ARGS__); \
            log->flush(); \
        } \
    } while (0);

#define LOG_DEBUG(format, ...)   LOG_BASE(Level::DEBUG, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)    LOG_BASE(Level::INFO, format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) LOG_BASE(Level::WARNING, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...)   LOG_BASE(Level::ERROR, format, ##__VA_ARGS__)


#endif //WEBSERVER_LOG_H
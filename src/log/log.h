#ifndef WEBSERVER_LOG_H
#define WEBSERVER_LOG_H

#include <mutex>
#include <string>
#include <thread>
#include <sys/time.h>
#include <cstring>
#include <cstdarg>
#include "block_queue.h"
#include "buffer.h"

class Log {
private:
    static constexpr int LOG_PATH_LEN = 256;    // 日志路径最大长度
    static constexpr int LOG_NAME_LEN = 256;    // 日志文件最大长度
    static constexpr int MAX_LINES = 50000;     // 单个日志文件最大行数

    const char* path;
    const char* suffix;

    int max_lines;
    int line_count;
    int today;

    bool is_open;
    Buffer buff;                                    // 临时格式化字符串的缓冲区
    int level;                                      // 日志级别 (0:Debug, 1:Info, 2:Warn, 3:Error)
    bool is_async;                                  // 是否开启异步模式

    FILE* fp;                                       // 底层文件指针
    std::unique_ptr<BlockQueue<std::string>> deque; // 异步阻塞队列
    std::unique_ptr<std::thread> write_thread;      // 后台写线程
    std::mutex mtx;                                 // 保护 fp_ 和 buff_ 的锁

    Log();
    virtual ~Log();

    void append_log_level_title(int _level);

    // 后台真正执行磁盘写入的循环
    void async_write();
public:
    // 初始化日志器：日志级别, 存放路径, 文件后缀, 阻塞队列容量
    void init(int _level, const char* _path = "./log_data", const char* _suffix = ".log", int _max_queue_capacity = 1024);

    // 单例模式获取实例
    static Log* instance();

    // 异步刷盘的后台线程执行函数
    static void flush_log_thread();

    // 写日志的核心函数
    void write(int _level, const char* _format, ...);

    // 强制刷新缓冲区到底层磁盘
    void flush();

    // 获取当前日志级别
    int get_level();

    // 设置日志级别
    void set_level(int _level);

    bool get_is_open() const { return is_open; }
};

// 便捷使用的宏定义 (只在等级符合要求时才执行真正地解析，节省性能)
#define LOG_BASE(level, format, ...) \
    do {\
        Log* log = Log::instance();\
        if (log->get_is_open() && log->get_level() <= level) {\
            log->write(level, format, ##__VA_ARGS__); \
            log->flush();\
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do { LOG_BASE(0, format, ##__VA_ARGS__) } while(0);
#define LOG_INFO(format, ...)  do { LOG_BASE(1, format, ##__VA_ARGS__) } while(0);
#define LOG_WARN(format, ...)  do { LOG_BASE(2, format, ##__VA_ARGS__) } while(0);
#define LOG_ERROR(format, ...) do { LOG_BASE(3, format, ##__VA_ARGS__) } while(0);

#endif //WEBSERVER_LOG_H
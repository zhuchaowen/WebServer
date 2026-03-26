#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <sys/stat.h>
#include <ctime>
#include "log.h"

Log::Log() 
    : line_count(0), today(0), is_running(false), level(Level::INFO), is_async(false),
      fp(nullptr), queue(nullptr), write_thread(nullptr) {}

Log::~Log()
{
    // 优雅退出异步线程
    if (write_thread && write_thread->joinable()) {
        // 关闭队列，阻止新日志进入，并唤醒后台线程
        queue->close();
        // 阻塞等待后台线程消费完剩余日志
        write_thread->join();
    }

    // 刷新缓冲区并关闭文件
    if (fp) {
        std::lock_guard<std::mutex> lock(mtx);

        fflush(fp);
        fclose(fp);
    }
}

Log* Log::instance() 
{
    static Log log;
    return &log;
}

void Log::append_log_level_title(Level _level)
{
    switch (_level) {
        case Level::DEBUG:
            buffer.append("[DEBUG]: ", 9);
            break;
        case Level::INFO:
            buffer.append("[INFO]: ", 8);
            break;
        case Level::WARNING:
            buffer.append("[WARNING]: ", 11);
            break;
        case Level::ERROR:
            buffer.append("[ERROR]: ", 9);
            break;
        default:
            buffer.append("[UNKNOWN]: ", 11);
            break;
    }
}

void Log::async_write()
{
    std::string str;
    // 只要队列没被 close 且还有数据，就不断 pop 出来写入文件
    while (queue->pop(str)) {
        std::lock_guard<std::mutex> lock(mtx);
        if (fp) {
            fputs(str.c_str(), fp);
        }
    }
}

void Log::init(Level _level, size_t _max_queue_capacity)
{
    level = _level;
    is_running = true;

    // 如果设置了队列容量且大于0，说明开启异步日志
    if (_max_queue_capacity > 0) {
        is_async = true;
        if (!queue) {
            queue = std::make_unique<BlockQueue>(_max_queue_capacity);
            // 启动后台写线程，调用 async_write
            write_thread = std::make_unique<std::thread>([this]() {
                this->async_write();
            });
        }
    } else {
        is_async = false;
    }

    // 创建日志目录 (如果不存在)
    mkdir(PATH, 0777);

    time_t timer = time(nullptr);
    struct tm* sys_time = localtime(&timer);
    struct tm t = *sys_time;
    today = t.tm_mday;

    // 拼装日志文件名：比如 ./log_data/2026_03_20.log
    char file_name[LOG_NAME_LEN]{};
    snprintf(file_name, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
        PATH, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, SUFFIX);

    std::lock_guard<std::mutex> lock(mtx);

    // 清理旧的缓冲
    buffer.retrieve_all();

    if (fp) {
        fflush(fp);
        fclose(fp);
    }

    fp = fopen(file_name, "a");
    if (!fp) {
        is_running = false;
    }
}

void Log::write(Level _level, const char* _format, ...)
{
    if (!is_running) {
        return;
    }
    
    struct timeval now{};
    gettimeofday(&now, nullptr);

    time_t sec = now.tv_sec;
    struct tm* sys_time = localtime(&sec);
    struct tm t = *sys_time;

    // 日志文件轮转逻辑 (跨天 或者 达到最大行数)
    if (today != t.tm_mday || (line_count && line_count % MAX_LINES == 0)) {
        std::unique_lock<std::mutex> lock(mtx);
        // 解锁，避免在拼接字符串时阻塞其他写日志的线程
        lock.unlock(); 
        
        char new_file[LOG_NAME_LEN];
        char tail[36]{0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (today != t.tm_mday) {
            // 跨天了
            snprintf(new_file, LOG_NAME_LEN - 1, "%s/%s%s", PATH, tail, SUFFIX);
            today = t.tm_mday;
            line_count = 0;
        } else {
            // 没跨天，但是行数满了
            snprintf(new_file, LOG_NAME_LEN - 1, "%s/%s-%d%s", PATH, tail, line_count / MAX_LINES, SUFFIX);
        }
        
        // 重新加锁
        lock.lock();

        if (fp) {
            fflush(fp);
            fclose(fp);
        }

        // 切换文件指针
        fp = fopen(new_file, "a");
    }
    
    // 格式化当前这条日志的内容
    {
        std::lock_guard<std::mutex> lock(mtx);
        ++line_count;
        
        // 写入时间头: YYYY-MM-DD HH:MM:SS.uuuuuu 
        buffer.ensure_writable(128);
        int n = snprintf(buffer.begin_write(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buffer.have_written(n);
        
        // 写日志级别前缀
        append_log_level_title(_level);

        // 解析用户传入的可变参数 (格式化字符串)
        va_list va;
        va_start(va, _format);
        
        int m = vsnprintf(buffer.begin_write(), buffer.writable_bytes(), _format, va);
        va_end(va);

        // 如果动态缓冲区不够，强制扩容后再写一次
        if (static_cast<size_t>(m) >= buffer.writable_bytes()) {
            // 留出换行符的空间
            buffer.ensure_writable(m + 1); 
            
            va_start(va, _format);
            m = vsnprintf(buffer.begin_write(), buffer.writable_bytes(), _format, va);
            va_end(va);
        }
        
        buffer.have_written(m);
        
        // 追加换行符
        buffer.append("\n", 1);

        // 将这行完整的日志交给异步队列（或直接写磁盘）
        if (is_async && queue && !queue->full()) {
            // 取出所有数据转为 string 送入阻塞队列
            queue->push(buffer.retrieve_all_to_string());
        } else {
            // 同步退化：如果未开启异步，或异步队列满了，直接同步写入磁盘，避免业务线程被长久阻塞
            if (fp) {
                fputs(buffer.const_begin_read(), fp);
            }
        }

        // 清空缓冲区留给下次使用
        buffer.retrieve_all();
    }
}

void Log::flush()
{
    std::lock_guard<std::mutex> lock(mtx);
    if (fp) {
        fflush(fp);
    }
}
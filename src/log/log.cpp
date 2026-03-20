#include "log.h"
#include <sys/stat.h> 

Log::Log() 
{
    line_count = 0;
    is_async = false;
    write_thread = nullptr;
    deque = nullptr;
    today = 0;
    fp = nullptr;
}

Log::~Log() 
{
    if (write_thread && write_thread->joinable()) {
        while (!deque->empty()) {
            deque->flush(); // 等待队列里的内容写完
        }
        deque->close();
        write_thread->join();
    }
    if (fp) {
        std::lock_guard<std::mutex> lock(mtx);
        flush();
        fclose(fp);
    }
}

Log* Log::instance() 
{
    static Log inst;
    return &inst;
}

void Log::flush_log_thread() 
{
    Log::instance()->async_write();
}

void Log::init(int _level, const char* _path, const char* _suffix, int _max_queue_capacity) 
{
    is_open = true;
    level = _level;
    
    // 如果设置了队列容量且大于0，说明开启异步日志
    if (_max_queue_capacity > 0) {
        is_async = true;
        if (!deque) {
            deque = std::make_unique<BlockQueue<std::string>>(_max_queue_capacity);
            write_thread = std::make_unique<std::thread>(flush_log_thread);
        }
    } else {
        is_async = false;
    }

    line_count = 0;
    path = _path;
    suffix = _suffix;

    time_t timer = time(nullptr);
    struct tm* sys_time = localtime(&timer);
    struct tm t = *sys_time;
    today = t.tm_mday;

    // 创建日志目录 (如果不存在)
    mkdir(path, 0777);

    // 拼装日志文件名：比如 ./log_data/2026_03_20.log
    char file_name[LOG_NAME_LEN] = {0};
    snprintf(file_name, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", path, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, suffix);

    {
        std::lock_guard<std::mutex> lock(mtx);
        buff.retrieve_all();
        if (fp) { 
            flush();
            fclose(fp); 
        }
        fp = fopen(file_name, "a");
        if (fp == nullptr) {
            mkdir(path, 0777); // 再试一次创建目录
            fp = fopen(file_name, "a");
        } 
        assert(fp != nullptr);
    }
}

void Log::write(int _level, const char* _format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);
    time_t t_sec = now.tv_sec;
    struct tm* sys_time = localtime(&t_sec);
    struct tm t = *sys_time;

    // 日志按天轮转或按最大行数分割
    if (today != t.tm_mday || (line_count && (line_count % MAX_LINES == 0))) {
        std::unique_lock<std::mutex> lock(mtx);
        lock.unlock();
        
        char new_file[LOG_NAME_LEN];
        char tail[36] = {0};
        snprintf(tail, 36, "%04d_%02d_%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

        if (today != t.tm_mday) {
            snprintf(new_file, LOG_NAME_LEN - 72, "%s/%s%s", path, tail, suffix);
            today = t.tm_mday;
            line_count = 0;
        } else {
            snprintf(new_file, LOG_NAME_LEN - 72, "%s/%s-%d%s", path, tail, (line_count / MAX_LINES), suffix);
        }
        
        lock.lock();
        flush();
        fclose(fp);
        fp = fopen(new_file, "a");
        assert(fp != nullptr);
    }

    // 格式化当前这条日志的内容
    {
        std::lock_guard<std::mutex> lock(mtx);
        line_count++;
        
        // 1. 写时间前缀
        int n = snprintf(buff.begin_write(), 128, "%d-%02d-%02d %02d:%02d:%02d.%06ld ",
                         t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                         t.tm_hour, t.tm_min, t.tm_sec, now.tv_usec);
        buff.has_written(n);
        
        // 2. 写日志级别前缀
        append_log_level_title(level);

        // 3. 解析用户传入的可变参数 (格式化字符串)
        va_list valst;
        va_start(valst, _format);
        int m = vsnprintf(buff.begin_write(), buff.writable_bytes(), _format, valst);
        va_end(valst);

        // 如果动态缓冲区不够，强制扩容后再写一次
        if (static_cast<size_t>(m) >= buff.writable_bytes()) {
            buff.ensure_writable(m + 2); // 留出换行符的空间
            va_start(valst, _format);
            m = vsnprintf(buff.begin_write(), buff.writable_bytes(), _format, valst);
            va_end(valst);
        }
        buff.has_written(m);
        
        // 4. 追加换行符
        buff.append("\n\0", 2);

        // 5. 将这行完整的日志交给异步队列（或直接写磁盘）
        if (is_async && deque && !deque->full()) {
            deque->push(buff.retrieve_all_to_str());
        } else {
            fputs(buff.peek(), fp); // 同步降级
        }
        buff.retrieve_all(); // 清空缓冲区留给下次使用
    }
}

void Log::append_log_level_title(int _level)
{
    switch (_level) {
        case 0: buff.append("[debug]: ", 18); break; 
        case 1: buff.append("[info]: ", 18); break; 
        case 2: buff.append("[warn]: ", 18); break; 
        case 3: buff.append("[error]: ", 18); break; 
        default:buff.append("[info]: ", 18); break;
    }
}

void Log::flush()
{
    if (is_async) {
        // 唤醒消费者，尽力去刷
        deque->flush();
    }
    std::lock_guard<std::mutex> lock(mtx);
    fflush(fp);
}

void Log::async_write()
{
    std::string str;
    while (deque->pop(str)) {
        std::lock_guard<std::mutex> lock(mtx);
        fputs(str.c_str(), fp);
    }
}

int Log::get_level()
{
    std::lock_guard<std::mutex> lock(mtx);
    return level;
}

void Log::set_level(int _level)
{
    std::lock_guard<std::mutex> lock(mtx);
    level = _level;
}
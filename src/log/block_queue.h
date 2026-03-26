#ifndef WEBSERVER_BLOCK_QUEUE_H
#define WEBSERVER_BLOCK_QUEUE_H

#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>

// 线程安全的有界阻塞队列（生产者-消费者模型）
class BlockQueue {
private:
    std::deque<std::string> queue;      // 底层双端队列
    size_t max_capacity;                // 队列最大容量
    
    std::mutex mtx;                     // 保护队列的互斥锁
    std::condition_variable not_empty;  // 队列非空条件变量
    std::condition_variable not_full;   // 队列未满条件变量
    
    bool is_close;                      // 是否关闭队列
public:
    explicit BlockQueue(size_t _max_capacity = 4096) 
        : max_capacity(_max_capacity), is_close(false) 
    {
        if (max_capacity == 0) {
            max_capacity = 4096;
        }
    }
    
    ~BlockQueue() { close(); }
    
    size_t size() 
    {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }
    
    size_t capacity() const noexcept { return max_capacity; }
        
    bool empty() 
    {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.empty();
    }
    
    bool full() 
    {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size() >= max_capacity;
    }
    
    void clear()
    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.clear();
    }

    // 不再接受新任务
    void close() 
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            is_close = true;
        }

        // 唤醒所有线程
        not_empty.notify_all();
        not_full.notify_all();
    }
    
    bool push(const std::string& log)
    {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待队列未满 或 队列关闭
        not_full.wait(lock, [this] {
            return queue.size() < max_capacity || is_close;
        });

        if (is_close) {
            return false;
        }

        queue.push_back(log);

        lock.unlock();
        not_empty.notify_one();

        return true;
    }

    bool push(std::string&& log)
    {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待队列未满 或 队列关闭
        not_full.wait(lock, [this] {
            return queue.size() < max_capacity || is_close;
        });

        if (is_close) {
            return false;
        }

        queue.push_back(std::move(log));

        lock.unlock();
        not_empty.notify_one();

        return true;
    }
    
    bool pop(std::string& log)
    {
        std::unique_lock<std::mutex> lock(mtx);

        // 等待队列非空 或 队列关闭
        not_empty.wait(lock, [this] {
            return !queue.empty() || is_close;
        });

        // 处理完剩余任务
        if (queue.empty()) {
            return false;
        }

        log = std::move(queue.front());
        queue.pop_front();

        lock.unlock();
        not_full.notify_one();
        
        return true;
    }
};


#endif //WEBSERVER_BLOCK_QUEUE_H
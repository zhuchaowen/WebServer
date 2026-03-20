#ifndef WEBSERVER_BLOCK_QUEUE_H
#define WEBSERVER_BLOCK_QUEUE_H

#include <mutex>
#include <deque>
#include <condition_variable>
#include <cassert>

template<class T>
class BlockQueue {
private:
    std::deque<T> queue;                   // 底层双端队列
    size_t max_capacity;                   // 队列最大容量
    std::mutex mtx;                        // 保护队列的互斥锁
    bool is_close;                         // 队列关闭标志

    std::condition_variable cond_consumer; // 消费者条件变量 (非空)
    std::condition_variable cond_producer; // 生产者条件变量 (非满)
public:
    explicit BlockQueue(size_t _max_capacity = 1000) : max_capacity(_max_capacity)
    {
        assert(max_capacity > 0);
        is_close = false;
    }

    ~BlockQueue() { close(); }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mtx);
        queue.clear();
    }

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

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(mtx);
            queue.clear();
            is_close = true;
        }

        cond_producer.notify_all();
        cond_consumer.notify_all();
    }

    // 唤醒消费者线程，让它尽快处理完队列中的剩余任务
    void flush() 
    {
        cond_consumer.notify_one();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }

    size_t capacity() const { return max_capacity; }

    void push(const T& item)
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (queue.size() >= max_capacity) {
            cond_producer.wait(lock);
        }

        queue.push_back(item);
        cond_consumer.notify_one();
    }

    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mtx);
        while (queue.empty()) {
            if (is_close) {
                return false;
            }

            cond_consumer.wait(lock);
        }

        item = queue.front();
        queue.pop_front();
        cond_producer.notify_one();

        return true;
    }
};

#endif //WEBSERVER_BLOCK_QUEUE_H
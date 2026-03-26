#ifndef WEBSERVER_THREAD_POOL_H
#define WEBSERVER_THREAD_POOL_H

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <stdexcept>

// 简单线程池实现（生产者-消费者模型）
// 使用条件变量实现阻塞等待
// 主线程负责投递任务
// 工作线程负责从任务队列取任务并执行
class ThreadPool {
private:
    size_t max_queue_size;                      // 任务队列最大容量

    bool stop;                                  // 线程池停止标志位

    std::mutex mtx;                             // 保护共享资源(任务队列)的互斥锁
    std::condition_variable not_empty;          // 任务就绪(队列非空)条件变量

    std::queue<std::function<void()>> tasks;    // 任务队列

    // 必须确保前面所有的锁、队列、标志位都构造完毕后，再启动线程
    std::vector<std::thread> threads;           // 工作线程数组

    // 工作线程执行函数
    // 执行逻辑： 1. 等待任务到来 2. 如果 stop=true 且 队列为空 → 退出线程 3. 否则取出任务执行
    // 使用 while(true) + 条件判断实现优雅退出，处理完已有任务后再退出，不丢任务
    void worker()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mtx);

                // 阻塞等待，直到被唤醒且 (有任务到来 或 收到停止信号)
                not_empty.wait(lock, [this] {
                    return !tasks.empty() || stop;
                });

                // 优雅退出：收到停止信号 且 队列中彻底没有任务时，线程安全退出
                if (stop && tasks.empty()) {
                    return;
                }

                // 取出一个任务
                task = std::move(tasks.front());
                tasks.pop();
            } // 离开作用域，自动释放 mtx

            // 锁外执行：绝不允许带着锁去执行业务逻辑（如 HTTP 解析），防阻塞
            if (task) {
                task();
            }
        }
    }

    // 优雅地关闭线程池
    void shutdown()
    {
        {
            // 在锁的保护下修改标志位，保证与 worker 线程中的 wait 条件严格同步
            std::lock_guard<std::mutex> lock(mtx);

            if (stop) {
                return; // 已经被关闭过，直接返回
            }
            stop = true;
        }

        // 唤醒所有正在 wait 的工作线程
        not_empty.notify_all();

        for (std::thread& t : threads) {
            if (t.joinable()) {
                // 等待所有工作线程执行完剩余任务后退出
                t.join();
            }
        }
    }
public:
    // 初始化并启动指定数量的工作线程
    explicit ThreadPool(size_t _number = 8, size_t _max_queue_size = 10000)
        : max_queue_size(_max_queue_size), stop(false)
    {
        threads.reserve(_number);

        for (size_t i = 0; i < _number; ++i) {
            try {
                // 工作线程启动，执行 worker 内部逻辑
                threads.emplace_back([this] { worker(); });
            } catch (...) {
                // 异常安全：如果某个线程创建失败，必须唤醒并回收已创建的线程
                shutdown();

                throw std::runtime_error("ThreadPool failed to create threads");
            }
        }
    }

    ~ThreadPool() { shutdown(); }

    // 向线程池添加任务
    template <typename F>
    bool add_task(F&& task)
    {
        std::unique_lock<std::mutex> lock(mtx);

        // 如果线程池已停止，或者任务队列达到了容量上限，实施拒绝策略
        if (stop || tasks.size() >= max_queue_size) {
            return false;
        }

        // 使用 std::forward 完美转发，将任务移入队列，避免不必要的拷贝开销
        tasks.emplace(std::forward<F>(task));

        lock.unlock();          // 先解锁
        not_empty.notify_one(); // 再通知一个处于空闲状态的工作线程

        return true;
    }
};


#endif //WEBSERVER_THREAD_POOL_H
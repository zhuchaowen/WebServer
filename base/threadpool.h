#pragma once
#include <iostream>
#include <vector>
#include <queue>
#include <exception>
#include <thread>
#include <mutex>
#include <condition_variable>

/*
 * 简单线程池实现（生产者-消费者模型）
 *  - 主线程负责投递任务
 *  - 工作线程负责从任务队列取任务并执行
 *  - 使用条件变量实现阻塞等待
 *  - 支持优雅关闭
 */
template<typename T>
class threadpool {
private:
    int number;                         // 线程数量
    std::vector<std::thread> threads;   // 工作线程数组

    int max_requests;                   // 任务队列最大容量   
    std::queue<T*> request_queue;       // 任务队列（只有使用权）

    std::mutex mtx;                     // 保护任务队列的互斥锁
    std::condition_variable empty;      // 队列非空条件变量

    bool stop;                          // 线程池是否停止

    /*
     * 工作线程执行函数
     *
     * 执行逻辑：
     * 1. 等待任务到来
     * 2. 如果 stop=true 且 队列为空 → 退出线程
     * 3. 否则取出任务执行
     *
     * - 使用 while(true) + 条件判断实现优雅退出
     * - 处理完已有任务后再退出，不丢任务
     */
    void worker()
    {
        while (true) {
            T* request;

            {
                std::unique_lock<std::mutex> lock(mtx);

                // 等待直到：1) 收到停止信号 2) 队列非空
                empty.wait(lock, [this]() {
                    return stop || !request_queue.empty();
                    });

                // 如果停止且队列为空，线程安全退出
                if (stop && request_queue.empty()) {
                    break;
                }
                // 取出一个任务
                request = request_queue.front();
                request_queue.pop();
            }   // 离开作用域自动解锁

            // 在锁外执行任务（避免长时间占用锁）
            if (request) {
                request->process();
            }
        }
    }
public:
    // 创建指定数量的工作线程
    threadpool(int _number = 6, int _max_requests = 5000)
        : number(_number), max_requests(_max_requests), stop(false)
    {
        threads.reserve(number);
        for (int i = 0; i < number; ++i) {
            std::cout << "create the " << i + 1 << "th thread" << std::endl;

            // 启动线程，执行当前对象的 worker 成员函数
            threads.emplace_back(&threadpool::worker, this);
        }
    }

    /*
     * 析构函数
     *
     * 优雅关闭流程：
     * 1. 加锁修改 stop = true
     * 2. 唤醒所有阻塞线程
     * 3. join 等待线程退出
     */
    ~threadpool() 
    { 
        {
            std::lock_guard<std::mutex> lock(mtx);
            stop = true;
        }

        // 唤醒所有等待线程
        empty.notify_all();

        // 等待所有线程结束
        for (std::thread& t : threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    /*
     * 向线程池添加任务
     *
     * 返回：
     *   true  -> 成功加入队列
     *   false -> 队列满 或 线程池已停止
     */
    bool add_request(T* request)
    {
        std::unique_lock<std::mutex> lock(mtx);
        
        // 队列满 或 已停止
        if (request_queue.size() == max_requests || stop) {
            return false;
        }

        // 添加任务
        request_queue.push(request);

        lock.unlock();      // 先解锁
        empty.notify_one(); // 再通知一个工作线程

        return true;
    }
};
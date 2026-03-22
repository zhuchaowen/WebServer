#ifndef WEBSERVER_HEAPTIMER_H
#define WEBSERVER_HEAPTIMER_H

#include <queue>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <vector>
#include <mutex>

using timeout_callback = std::function<void()>;
using time_clock = std::chrono::high_resolution_clock;
using ms = std::chrono::milliseconds;
using time_stamp = time_clock::time_point;

struct TimerNode {
    int id;                 // 用来标记定时器，这里使用客户端的 fd
    time_stamp expires;     // 绝对的过期时间
    timeout_callback cb;    // 超时后的回调函数 (比如关闭连接)
    
    // 重载比较运算符，用于堆的排序 (小根堆：时间越早的越在堆顶)
    bool operator<(const TimerNode& t) const {
        return expires < t.expires;
    }
};

class HeapTimer {
private:
    std::vector<TimerNode> heap;
    // 映射 fd 到它在 heap 数组中的索引，实现 O(1) 的查找，便于极速更新定时器
    std::unordered_map<int, size_t> ref; 
    // 新增一把互斥锁，用于保护 heap 和 ref
    std::mutex mtx; 

    // 堆的内部操作：向上调整和向下调整
    void remove(size_t i);
    void sift_up(size_t i);
    bool sift_down(size_t i, size_t n);
    void swap_node(size_t i, size_t j);
public:
    HeapTimer() { heap.reserve(64); }
    ~HeapTimer() { clear(); }
    
    // 调整指定 id(fd) 的定时器过期时间
    void adjust(int id, int timeout);

    // 新增一个定时器
    void add(int id, int timeout, const timeout_callback& cb);

    // 删除指定 id(fd) 的定时器并触发回调
    void do_work(int id);

    // 清除所有定时器
    void clear();

    // 核心：处理所有已经超时的定时器
    void tick();

    // 核心：获取下一个即将超时的定时器还要等多久 (用于传给 epoll_wait)
    int get_next_tick();
};

#endif //WEBSERVER_HEAPTIMER_H
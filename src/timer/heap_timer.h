#ifndef WEBSERVER_HEAP_TIMER_H
#define WEBSERVER_HEAP_TIMER_H


#include <vector>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <mutex>

using timeout_callback = std::function<void()>;         // 超时后执行的回调函数 (如关闭连接)
using high_clock = std::chrono::high_resolution_clock;  // 底层提供的高精度时钟
using ms = std::chrono::milliseconds;                   // 相对等待时间，单位为毫秒
using time_stamp = high_clock::time_point;              // 定时器到期的确切时间点

struct TimerNode {
    int id;                 // 用来标记定时器，这里使用客户端的 fd
    time_stamp expires;     // 绝对的过期时间
    timeout_callback cb;    // 超时后的回调函数 (比如关闭连接)

    // 重载比较运算符，用于堆的排序 (小根堆：时间越早的越在堆顶)
    bool operator<(const TimerNode& t) const noexcept { return expires < t.expires; }
};

class HeapTimer {
private:
    std::vector<TimerNode> timers;              // 内部维护的小根堆数组
    std::unordered_map<int, size_t> timer_ids;  // fd 映射到 timers 数组的索引
    std::mutex mtx;                             // 互斥锁，用于保护 timers 和 timers_id

    // 交换堆中的两个节点，并同步更新 timer_ids
    void swap_node(size_t i, size_t j) noexcept;
    // 向上调整 (新节点插入堆底时调用)
    void sift_up(size_t i) noexcept;
    // 向下调整 (删除堆顶元素或修改过期时间时调用)
    bool sift_down(size_t i, size_t n) noexcept;
    // 删除节点
    void remove(size_t i) noexcept;
public:
    // 稍微预分配一点空间，减少初次扩容开销
    HeapTimer() { timers.reserve(1024); }
    ~HeapTimer() { clear(); }

    // 调整指定 id 的定时器过期时间
    void adjust(int id, int timeout);
    // 新增一个定时器
    void add(int id, int timeout, const timeout_callback& cb);
    // 删除指定 id 的定时器并触发回调
    void do_work(int id);
    // 清除所有定时器
    void clear();

    // 处理所有已经超时的定时器
    void tick();
    // 返回下一个定时器距离现在还有多少毫秒，方便直接传给 epoll_wait
    int get_next_tick();
};


#endif //WEBSERVER_HEAP_TIMER_H
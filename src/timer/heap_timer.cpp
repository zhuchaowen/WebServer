#include "heap_timer.h"

void HeapTimer::swap_node(size_t i, size_t j) noexcept
{
    if (i >= timers.size() || j >= timers.size() || i == j) {
        return;
    }

    std::swap(timers[i], timers[j]);

    // 同步更新哈希表里的下标映射
    timer_ids[timers[i].id] = i;
    timer_ids[timers[j].id] = j;
}

void HeapTimer::sift_up(size_t i) noexcept
{
    if (i >= timers.size()) {
        return;
    }

    size_t p = (i - 1) / 2;
    while (i > 0 && timers[i] < timers[p]) {
        swap_node(i, p);
        i = p;
        p = (i - 1) / 2;
    }
}

bool HeapTimer::sift_down(size_t i, size_t n) noexcept
{
    if (i >= timers.size() || n > timers.size()) {
        return false;
    }

    size_t j = i;
    size_t c = 2 * j + 1;
    while (c < n) {
        // 找到两个子节点中较小的一个
        if (c + 1 < n && timers[c + 1] < timers[c]) {
            ++c;
        }

        // 如果当前节点比最小的子节点还小，说明调整完毕
        if (timers[j] < timers[c]) {
            break;
        }
        
        swap_node(j, c);
        j = c;
        c = 2 * j + 1;
    }
    
    return j > i;
}

void HeapTimer::remove(size_t i) noexcept
{
    if (timers.empty() || i >= timers.size()) {
        return;
    }

    // 将要删除的节点与队尾节点交换
    size_t t = timers.size() - 1;

    if (i < t) {
        swap_node(i, t);

        // 尝试向下调整，如果没动，再尝试向上调整
        if (!sift_down(i, t)) {
            sift_up(i);
        }
    }

    // 删除队尾元素
    timer_ids.erase(timers.back().id);
    timers.pop_back();
}

void HeapTimer::adjust(int id, int timeout)
{
    std::lock_guard<std::mutex> lock(mtx);

    // 只有存在的定时器才能更新时间（比如活跃的 HTTP 请求）
    if (id < 0 || timers.empty() || !timer_ids.contains(id)) {
        return;
    }

    timers[timer_ids[id]].expires = high_clock::now() + ms(timeout);

    // 时间延后了，所以只需要向下调整
    sift_down(timer_ids[id], timers.size());
}

void HeapTimer::add(int id, int timeout, const timeout_callback& cb)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (id < 0) {
        return;
    }

    if (timer_ids.contains(id)) {
        // 如果已经存在，直接更新时间并调整堆
        size_t i = timer_ids[id];
        timers[i].expires = high_clock::now() + ms(timeout);
        timers[i].cb = cb;

        if (!sift_down(i, timers.size())) {
            sift_up(i);
        }
    } else {
        // 新节点放到队尾，然后向上调整
        size_t i = timers.size();
        timer_ids[id] = i;
        timers.push_back({id, high_clock::now() + ms(timeout), cb});
        sift_up(i);
    }
}

void HeapTimer::do_work(int id)
{
    // 用于临时存放回调函数
    timeout_callback cb;

    {
        // 锁的作用域仅限于取出回调函数和操作堆
        std::lock_guard<std::mutex> lock(mtx);

        if (timers.empty() || !timer_ids.contains(id)) {
            return;
        }

        size_t i = timer_ids[id];
        // 把回调函数拷贝出来
        cb = timers[i].cb;
        // 从堆中删除
        remove(i);
    }

    // 在无锁状态下安全地触发回调，彻底杜绝死锁风险！
    if (cb) {
        cb();
    }
}

void HeapTimer::clear()
{
    std::lock_guard<std::mutex> lock(mtx);

    timers.clear();
    timer_ids.clear();
}

void HeapTimer::tick()
{
    // 不断循环，直到堆顶没有超时的定时器为止
    while (true) {
        timeout_callback cb;

        {
            // 锁的粒度控制在每次判断和取出一个定时器
            std::lock_guard<std::mutex> lock(mtx);

            if (timers.empty()) {
                break;
            }

            TimerNode& node = timers.front();
            // 如果堆顶都没超时，说明后面的也都没超时，退出循环
            if (std::chrono::duration_cast<ms>(node.expires - high_clock::now()).count() > 0) {
                break;
            }

            cb = node.cb;
            remove(0);
        }

        // 在无锁状态下执行回调
        if (cb) {
            cb();
        }
    }
}

int HeapTimer::get_next_tick()
{
    // 先清理掉当前已经超时的
    // tick() 内部有自己的锁，执行完会自动释放
    tick();

    // 再次加锁保护后续的读取
    std::lock_guard<std::mutex> lock(mtx);

    int timeout = -1;
    if (!timers.empty()) {
        // 计算堆顶元素还要多久超时
        timeout = static_cast<int>(std::chrono::duration_cast<ms>(timers.front().expires - high_clock::now()).count());

        // 如果恰巧碰到了过期但还没被 tick 清理的极端并发情况，防止返回负数！
        if (timeout < 0) {
            timeout = 0;
        }
    }

    return timeout;
}
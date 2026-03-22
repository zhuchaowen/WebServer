#include "heaptimer.h"

void HeapTimer::swap_node(size_t i, size_t j) 
{
    if (i >= heap.size() || j >= heap.size()) return;
    
    std::swap(heap[i], heap[j]);
    
    // 同步更新哈希表中的索引
    ref[heap[i].id] = i;
    ref[heap[j].id] = j;
}

void HeapTimer::sift_up(size_t i) 
{
    if (i >= heap.size()) return;
    
    size_t parent = (i - 1) / 2;
    while (i > 0 && heap[i] < heap[parent]) {
        swap_node(i, parent);
        i = parent;
        parent = (i - 1) / 2;
    }
}

bool HeapTimer::sift_down(size_t i, size_t n) 
{
    if (i >= heap.size() || n > heap.size()) return false;
    
    size_t index = i;
    size_t child = index * 2 + 1;
    while (child < n) {
        // 找到两个子节点中较小的一个
        if (child + 1 < n && heap[child + 1] < heap[child]) {
            child++;
        }
        // 如果当前节点比最小的子节点还小，说明调整完毕
        if (heap[index] < heap[child]) {
            break;
        }
        swap_node(index, child);
        index = child;
        child = index * 2 + 1;
    }
    return index > i;
}

void HeapTimer::remove(size_t index) 
{
    if (heap.empty() || index >= heap.size()) return;
    
    // 将要删除的节点与队尾节点交换
    size_t n = heap.size() - 1;
    
    if (index < n) {
        swap_node(index, n);
        // 尝试向下调整，如果没动，再尝试向上调整
        if (!sift_down(index, n)) {
            sift_up(index);
        }
    }
    
    // 删除队尾元素
    ref.erase(heap.back().id);
    heap.pop_back();
}

void HeapTimer::add(int id, int timeout, const timeout_callback& cb) 
{
    std::lock_guard<std::mutex> lock(mtx);

    if (id < 0) return;
    
    size_t i;
    if (ref.count(id)) {
        // 如果已经存在，直接更新时间并调整堆
        i = ref[id];
        heap[i].expires = time_clock::now() + ms(timeout);
        heap[i].cb = cb;
        if (!sift_down(i, heap.size())) {
            sift_up(i);
        }
    } else {
        // 新节点放到队尾，然后向上调整
        i = heap.size();
        ref[id] = i;
        heap.push_back({id, time_clock::now() + ms(timeout), cb});
        sift_up(i);
    }
}

void HeapTimer::adjust(int id, int timeout)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (id < 0 || heap.empty() || ref.count(id) == 0) return;

    // 只有存在的定时器才能更新时间（比如活跃的 HTTP 请求）
    if (heap.empty() || ref.count(id) == 0) {
        return;
    }
    heap[ref[id]].expires = time_clock::now() + ms(timeout);
    // 时间延后了，所以只需要向下调整
    sift_down(ref[id], heap.size());
}

void HeapTimer::do_work(int id)
{
    std::lock_guard<std::mutex> lock(mtx);

    if (heap.empty() || ref.count(id) == 0) {
        return;
    }

    size_t i = ref[id];
    TimerNode node = heap[i];
    node.cb(); // 触发回调函数 (关闭连接)
    remove(i);   // 从堆中删除
}

void HeapTimer::clear()
{
    std::lock_guard<std::mutex> lock(mtx);

    ref.clear();
    heap.clear();
}

void HeapTimer::tick()
{
    std::lock_guard<std::mutex> lock(mtx);

    // 清除所有已经超时的定时器
    if (heap.empty()) return;
    
    while (!heap.empty()) {
        TimerNode node = heap.front();
        // 因为是小根堆，如果堆顶都没超时，剩下的肯定没超时
        if (std::chrono::duration_cast<ms>(node.expires - time_clock::now()).count() > 0) {
            break; 
        }
        node.cb();
        remove(0);
    }
}

int HeapTimer::get_next_tick()
{
    // 先清理掉当前已经超时的
    // tick() 内部有自己的锁，执行完会自动释放
    tick(); 

    // 再次加锁保护后续的读取
    std::lock_guard<std::mutex> lock(mtx); 
    
    int res = -1;
    if (!heap.empty()) {
        // 计算堆顶元素还要多久超时
        res = std::chrono::duration_cast<ms>(heap.front().expires - time_clock::now()).count();
        if (res < 0) {
            res = 0;
        }
    }
    return res;
}
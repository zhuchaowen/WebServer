#ifndef WEBSERVER_EPOLLER_H
#define WEBSERVER_EPOLLER_H


#include <unistd.h>
#include <sys/epoll.h>
#include <vector>

class Epoller {
private:
    int epoll_fd;                           // epoll 实例的底层文件描述符
    std::vector<struct epoll_event> events; // 用于存放 epoll_wait 返回的就绪事件数组
public:
    // 初始化 epoll 实例并分配事件缓冲区的初始大小
    // 默认开辟 10000 个事件的缓冲区
    explicit Epoller(int max_event = 10000)
        : epoll_fd(epoll_create(1)), events(max_event) {}

    // 释放 epoll 文件描述符
    ~Epoller() { close(epoll_fd); }

    // 将文件描述符 fd 及对应的监听事件注册到 epoll 树上
    bool add_fd(int fd, uint32_t events)
    {
        if (fd < 0) {
            return false;
        }

        epoll_event event{};
        event.data.fd = fd;
        event.events = events;

        return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
    }

    // 修改已在 epoll 树上的文件描述符 fd 的监听事件
    bool mod_fd(int fd, uint32_t events)
    {
        if (fd < 0) {
            return false;
        }

        epoll_event event{};
        event.data.fd = fd;
        event.events = events;

        return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
    }

    // 将文件描述符 fd 从 epoll 树上摘除
    bool del_fd(int fd)
    {
        if (fd < 0) {
            return false;
        }

        epoll_event event{};

        return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, &event) == 0;
    }

    // 阻塞等待事件发生 (timeoutMs = -1 表示死等，= 0 表示非阻塞轮询)
    int wait(int timeout = -1)
    {
        // epoll_wait 会将触发的事件填充到 events 数组中
        // &events[0] 获取底层数组的首地址
        return epoll_wait(epoll_fd, &events[0], static_cast<int>(events.size()), timeout);
    }

    // 获取第 i 个触发事件的底层文件描述符
    int get_event_fd(size_t i) const noexcept
    {
        if (i >= events.size()) {
            return -1;
        }

        return events[i].data.fd;
    }

    // 获取第 i 个触发事件的具体事件类型
    uint32_t get_events(size_t i) const noexcept
    {
        if (i >= events.size()) {
            return 0;
        }

        return events[i].events;
    }
};


#endif //WEBSERVER_EPOLLER_H
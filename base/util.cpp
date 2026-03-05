#include "util.h"

// 添加信号捕捉
void addsig(int sig, void (*handler)(int), bool restart)
{
    struct sigaction sa{};

    sa.sa_handler = handler;

    // 处理信号期间阻塞所有信号，防止嵌套
    sigfillset(&sa.sa_mask);

    // 是否自动重启被中断的系统调用
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }

    if (sigaction(sig, &sa, nullptr) == -1) {
        throw std::runtime_error("sigaction failed");
    }
}

// 设置文件描述符非阻塞
void setnonblocking(int fd)
{
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

// 向epoll中添加文件描述符
void addfd(int efd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }

    epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event);

    // 设置文件描述符非阻塞
    setnonblocking(fd);
}

// 从epoll中删除文件描述符
void delfd(int efd, int fd)
{
    epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
}

// 修改文件描述符
void modfd(int efd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

    // 重置socket上的EPOLLONESHOT事件，确保下一次可读时，EPOLLIN事件能被触发
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event);
}
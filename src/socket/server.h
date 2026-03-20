#ifndef WEBSERVER_SERVER_H
#define WEBSERVER_SERVER_H

#include <iostream>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <memory>
#include <cstring>
#include "epoller.h"
#include "threadpool.h"
#include "http_connect.h"

class Server {
private:
    int listen_fd;                                      // 监听套接字
    in_port_t port;                                     //
    bool is_close;                                      // 服务器是否关闭

    uint32_t listen_events;                             // 监听套接字的事件模式 (LT/ET)
    uint32_t client_events;                             // 客户端套接字的事件模式 (LT/ET + ONESHOT)

    std::unique_ptr<Epoller> epoller;                   // epoll 管理模块
    std::unique_ptr<ThreadPool> threadpool;             // 并发线程池
    std::unordered_map<int, HttpConnect> clients;       // 全局连接哈希表，通过 fd 映射 HttpConnect

    // 初始化相关
    bool init_socket();
    void init_event_mode(int _mode);
    void add_client(int _fd, sockaddr_in _addr);

    // 断开连接与错误处理
    void close_connect(HttpConnect* client) const;
    static void send_error(int fd, const char* info);

    // 事件处理的分发函数 (Reactor 核心)
    void deal_listen();                      // 处理新客户端连接
    void deal_read(HttpConnect* client);     // 读事件：将任务扔给线程池
    void deal_write(HttpConnect* client);    // 写事件：将任务扔给线程池

    // 工作线程真正执行的 I/O 与逻辑处理
    void on_read(HttpConnect* client) const;
    void on_write(HttpConnect* client) const;

    // 设置文件描述符非阻塞
    static void set_nonblock(int _fd);
public:
    // 初始化服务器：端口、触发模式、线程池大小
    Server(in_port_t _port, int _mode, int _number);
    ~Server();

    // 启动服务器的主事件循环
    void start();
};

#endif //WEBSERVER_SERVER_H
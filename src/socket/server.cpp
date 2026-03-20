#include "server.h"
#include "log.h"
#include <libgen.h>

Server::Server(in_port_t _port, int _mode, int _timeout, int _number)
    : listen_fd(-1), port(_port), timeout(_timeout), is_close(false),
      epoller(std::make_unique<Epoller>()),
      threadpool(std::make_unique<ThreadPool>(_number)),
      timer(std::make_unique<HeapTimer>())
{
    // 获取当前工作目录，设置给 HttpConnect 供解析文件用
    char cwd[MAX_PATH_LEN];
    if (!getcwd(cwd, MAX_PATH_LEN)) {
        throw std::runtime_error("getcwd");
    }
    HttpConnect::root_dir = std::string(dirname(cwd)) + "/resources/";

    init_event_mode(_mode);

    if (!init_socket()) {
        is_close = true;
    }
}

Server::~Server() 
{
    close(listen_fd);
    is_close = true;
}

void Server::set_nonblock(int _fd)
{
    int flag = fcntl(_fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(_fd, F_SETFL, flag);
}

// 核心 Reactor 事件循环
void Server::start() 
{
    LOG_INFO("========== Server Start ==========");
    LOG_INFO("Listen Port: %d", port);
    LOG_INFO("Resources Dir: %s", HttpConnect::root_dir.c_str());

    while (!is_close) {
        // 如果开启了定时器，就获取最近一个定时器的超时时间作为 epoll_wait 的 timeout
        // 这样既不会空转 CPU，又能在有人超时时准时醒来踢掉它
        int time_ms = -1;
        if (timeout > 0) {
            time_ms = timer->get_next_tick();
        }

        // 阻塞等待事件发生
        int event_count = epoller->wait(time_ms);
        
        for (int i = 0; i < event_count; i++) {
            int fd = epoller->get_event_fd(i);
            uint32_t events = epoller->get_events(i);
            
            if (fd == listen_fd) {
                // 有新客户端连接
                deal_listen(); 
            } else if (events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对端断开连接或发生错误
                epoller->del_fd(fd);
                close_connect(&clients[fd]); 
            } else if (events & EPOLLIN) {
                // 客户端发来了 HTTP 请求
                deal_read(&clients[fd]);  
            } else if (events & EPOLLOUT) {
                // 可以向客户端发回 HTTP 响应了
                deal_write(&clients[fd]); 
            }
        }
    }
}

// 初始化网络监听
bool Server::init_socket() 
{
    // 创建监听的套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        return false;
    }

    // 设置端口复用
    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    
    // 绑定
    sockaddr_in saddr{};
    if (port > MAX_PORT || port < MIN_PORT) {
        return false;
    }
    
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);
    saddr.sin_port = htons(port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)) < 0) {
        LOG_ERROR("Bind Port %d Failed!", port);
        return false;
    }
    
    if (listen(listen_fd, MAX_LISTEN_BACKLOG) < 0) {
        LOG_ERROR("Listen Port %d Failed!", port);
        return false;
    }

    epoller->add_fd(listen_fd, listen_events);
    set_nonblock(listen_fd);    // 必须设为非阻塞
    
    return true;
}

// 初始化事件触发模式
void Server::init_event_mode(int _mode) 
{
    // 基础事件：可读 + 对端断开连接
    listen_events = EPOLLIN | EPOLLRDHUP;
    
    // 客户端基础事件：还要加上 EPOLLONESHOT，确保一个 socket 同一时间只有一个线程处理
    client_events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT; 

    switch (_mode) {
        case 0: 
            // LT + LT
            break; 
        case 1:         
            // LT + ET
            client_events |= EPOLLET;
            break;
        case 2: 
            // ET + LT
            listen_events |= EPOLLET;
            break;
        case 3:    
            // ET + ET
            listen_events |= EPOLLET;
            client_events |= EPOLLET;
            break;
        default:
            client_events |= EPOLLET;
            break;
    }
}

void Server::add_client(int _fd, sockaddr_in _addr) 
{
    // 初始化 HTTP 对象
    clients[_fd].init(_fd, _addr); 

    // 如果是长连接模式且超时时间 > 0，为它挂载一个定时器
    if (timeout > 0) {
        // 绑定回调函数：当超时时，调用 close_connect(&clients[_fd])
        timer->add(_fd, timeout, [this, client = &clients[_fd]] { close_connect(client); });
    }

    epoller->add_fd(_fd, client_events); 
    // 客户端套接字设为非阻塞
    set_nonblock(_fd);
}

void Server::deal_listen() 
{
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    
    // 如果监听 fd 是 ET 模式，需要循环 accept 直到为空
    do {
        int fd = accept(listen_fd, reinterpret_cast<struct sockaddr *>(&addr), &len);
        if (fd <= 0) {
            return;
        }
        
        // 限制最大连接数
        if (HttpConnect::user_count >= MAX_FD) {
            send_error(fd, "Server busy!");
            LOG_WARN("Client %d connected but Server is Busy!", fd);

            // 继续清空处于排队状态的 accept 队列
            continue;
        }

        add_client(fd, addr);
        LOG_INFO("New Client Connected! fd: %d, IP: %s, Port: %d", fd, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    } while (listen_events & EPOLLET);
}

void Server::close_connect(HttpConnect* client) const
{
    assert(client);
    LOG_INFO("Client Quit! fd: %d", client->get_fd());
    epoller->del_fd(client->get_fd()); // 从 epoll 树移除
    client->close_connect();           // 关闭底层 socket 并释放 mmap 内存
}

void Server::send_error(int fd, const char* info) 
{
    send(fd, info, strlen(info), 0);
    close(fd);
}

// 任务分发 (主线程执行)
void Server::deal_read(HttpConnect* client) 
{
    if (timeout > 0) {
        timer->adjust(client->get_fd(), timeout);
    }

    // 将“读取并处理逻辑”打包扔进线程池，主线程绝不阻塞
    threadpool->add_task([this, client] { on_read(client); });
}

void Server::deal_write(HttpConnect* client) 
{
    if (timeout > 0) {
        timer->adjust(client->get_fd(), timeout);
    }

    // 将“发送数据”逻辑打包扔进线程池
    threadpool->add_task([this, client] { on_write(client); });
}

// 工作线程核心逻辑 (在 ThreadPool 中执行)
void Server::on_read(HttpConnect* client) const
{
    int save_errno = 0;
    // 调用 HttpConnect 的 read (利用 ET 模式循环读取数据到 Buffer)
    ssize_t ret = client->read(&save_errno);
    
    if (ret <= 0 && save_errno != EAGAIN) {
        // 读出错或断开
        LOG_DEBUG("Client fd: %d read error or disconnected.", client->get_fd());
        close_connect(client);
        return;
    }
    
    // 数据读取完毕，进行 HTTP 解析并生成响应
    if (client->process()) {
        // 解析和生成成功，准备好数据了，告诉 epoll 下次触发可写事件 EPOLLOUT
        epoller->mod_fd(client->get_fd(), client_events | EPOLLOUT);
    } else {
        // 请求不完整，继续监听读事件
        epoller->mod_fd(client->get_fd(), client_events | EPOLLIN);
    }
}

void Server::on_write(HttpConnect* client) const
{
    int save_errno = 0;
    // 调用 HttpConnect 的 write (利用 writev 发送 Buffer 和文件内存)
    ssize_t ret = client->write(&save_errno);
    
    // 如果发送完了所有数据
    if (client->is_write_complete()) {
        // 判断是否是长连接 (Keep-Alive)
        if (client->is_keep_alive()) {
            // 响应彻底发完了，客户端还要继续，把解析器清空，迎接下一个请求
            client->reset_request();

            // 是长连接，不断开，重置 socket 为监听读事件，等待下一条 HTTP 请求
            epoller->mod_fd(client->get_fd(), client_events | EPOLLIN);
            return;
        }
    } else if (ret < 0 && save_errno == EAGAIN) {
        // 数据如果太大没发完，且系统缓冲区满了 (EAGAIN)
        // 让 epoll 继续监听写事件，下次再发
        epoller->mod_fd(client->get_fd(), client_events | EPOLLOUT);
        return;
    }
    
    // 既没发完又不是 EAGAIN，或者不是长连接，直接关闭
    close_connect(client);
}

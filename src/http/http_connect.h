#ifndef WEBSERVER_HTTP_CONNECT_H
#define WEBSERVER_HTTP_CONNECT_H

#include <string>
#include <sys/types.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include "http_request.h"
#include "http_response.h"
#include "buffer.h"

class HttpConnect {
private:
    int fd;                 // 客户端套接字
    sockaddr_in addr;       // 客户端地址信息
    bool is_close;          // 连接是否已经关闭

    Buffer read_buffer;     // 读缓冲区
    Buffer write_buffer;    // 写缓冲区

    HttpRequest request;    // HTTP 请求解析器
    HttpResponse response;  // HTTP 响应生成器

    int iov_count;          // 准备发送的内存块数量 (1 或 2)
    iovec iov[2];           // iov[0] 用于响应头，iov[1] 用于文件内容
public:
    // 全局 epoll 文件描述符 (所有连接共享同一个 epoll 实例进行事件监听)
    static int epoll_fd;
    // 统计当前服务器的并发连接数
    static std::atomic<int> user_count;
    // 所有连接共享同一个网站根目录
    static std::string root_dir;

    HttpConnect() : fd(-1), is_close(true) {}
    ~HttpConnect() { close_connect(); }

    // 初始化连接（当 accept 接收到新客户端时调用）
    void init(int _fd, const sockaddr_in& _addr);

    // 关闭连接，释放 mmap 映射的内存和文件描述符
    void close_connect();

    // 核心 I/O 接口
    // 使用非阻塞方式读取数据到 read_buffer (采用 ET 模式的循环读取)
    ssize_t read(int* save_errno);

    // 使用非阻塞 writev 分散写，将 write_buffer (响应头) 和 mmap文件 (响应体) 发送出去
    ssize_t write(int* save_errno);

    // 处理业务逻辑：解析请求 -> 生成响应 -> 准备好待发送的 iov 数据块
    bool process();

    // 状态查询接口
    int get_fd() const noexcept { return fd; }
    int get_port() const noexcept { return ntohs(addr.sin_port); }
    const char* get_ip() const noexcept { return inet_ntoa(addr.sin_addr); }

    // 判断当前 HTTP 连接是否要求 Keep-Alive
    bool is_keep_alive() const { return request.is_keep_alive(); }

    // 判断是否已经将准备好的响应头和文件全部发送完毕
    bool is_write_complete() const { return iov[0].iov_len + iov[1].iov_len == 0; }
};

#endif //WEBSERVER_HTTP_CONNECT_H
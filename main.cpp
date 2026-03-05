#include "threadpool.h"
#include "http.h"
#include <cstring>
#include <charconv>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cerr << "Usage: ./server port\n";
        exit(-1);
    }

    // 获取端口号
    unsigned short port;

    size_t len = std::strlen(argv[1]);
    auto [ptr, ec] = std::from_chars(argv[1], argv[1] + len, port);

    if (ec != std::errc() || ptr != argv[1] + len || port == 0) {
        std::cerr << "Invalid port\n";
        exit(-1);
    }

    // 对SIGPIPE信号进行处理
    addsig(SIGPIPE, SIG_IGN);

    // 创建一个数组保存所有http连接信息
    // 对象的生命周期由主线程管理
    std::unique_ptr<http[]> clients = std::make_unique<http[]>(MAX_FD);

    // 创建线程池
    std::unique_ptr<threadpool<http>> pool = std::make_unique<threadpool<http>>(6, 5000);

    // 创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);

    // 设置端口复用
    int reuse = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 绑定
    sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(port);
    bind(lfd, (sockaddr *)&saddr, sizeof(saddr));

    listen(lfd, 1024);

    // 创建epoll对象
    epoll_event epevs[MAX_EVENT_NUMBER];
    int efd = epoll_create(1);
    http::epoll_fd = efd;

    // 将监听的文件描述符添加到epoll对象中
    addfd(efd, lfd, false);

    while (true) {
        int num = epoll_wait(efd, epevs, MAX_EVENT_NUMBER, -1);

        // 循环遍历事件数组
        for (int i = 0; i < num; ++i) {
            int fd = epevs[i].data.fd;
            if (fd == lfd) {
                // 有客户端连接进来
                sockaddr_in caddr;
                socklen_t len = sizeof(caddr);
                int cfd = accept(lfd, (sockaddr *)&caddr, &len);

                if (http::user_count >= MAX_FD) {
                    // 目前连接数满了
                    // 给客户端写一个信息：服务端正忙
                    close(cfd);
                    continue;
                }
                
                // 将新的客户端连接数据初始化放进数组中
                clients[cfd].init(cfd);
            } else if (epevs[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 对方异常断开或错误等事件
                clients[fd].close_connection();
            }  else if (epevs[i].events & EPOLLIN) {
                if (clients[fd].read()) {
                    // 一次性把所有数据都读完
                    pool->add_request(&clients[fd]);
                } else {
                    clients[fd].close_connection();
                }
            } else if (epevs[i].events & EPOLLOUT) {
                // 一次性把所有数据都写完
                if (!clients[fd].write()) {
                    clients[fd].close_connection();
                }
            }
        }
    }

    close(efd);
    close(lfd);

    return 0;
}
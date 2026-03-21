#include <iostream>
#include <csignal>
#include <stdexcept>
#include "server.h"
#include "log.h"

// 添加信号捕捉
static void add_signal(int sig, void (*handler)(int), bool restart = true)
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

int main(int argc, char* argv[])
{
    // 设置默认端口，允许通过命令行覆盖 (例如: ./server 8080)
    int port = 10000;
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {
            std::cerr << "Invalid port number!" << std::endl;
            return EXIT_FAILURE;
        }
    }

    // 注册信号处理机制
    add_signal(SIGPIPE, SIG_IGN);

    // 防止 mmap 访问被截断的文件时引发服务端崩溃
    add_signal(SIGBUS, SIG_IGN);

    // 初始化日志系统
    // 参数：级别(1:INFO), 目录, 后缀, 异步队列大小
    Log::instance()->init(1, "./log_data", ".log", 4096);

    // 记录启动信息到文件
    LOG_INFO("========== Server Init ==========");
    LOG_INFO("Port: %d", port);
    LOG_INFO("Log System: Async Mode, Level: INFO");

    try {
        // 初始化 Server 实例
        // 参数设计：(端口号, 触发模式(1代表LT+ET), 线程池大小)
        Server server(port, 1, 6000, 8);

        // 启动主线程 Reactor 事件循环 (死循环，除非发生严重错误)
        server.start();
    } catch (const std::exception& e) {
        // 捕捉初始化或运行期间抛出的严重异常
        // 记录致命异常到日志
        LOG_ERROR("Server Fatal Error: %s", e.what());
        return EXIT_FAILURE;
    }

    LOG_INFO("========== Server Stopped ==========");

    return EXIT_SUCCESS;
}
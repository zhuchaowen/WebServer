# C++ Lightweight Web Server

一个基于 C++11 编写的轻量级、高性能并发 Web 服务器，运行于 Linux 环境。项目采用 **多线程 Reactor 事件处理模式**，旨在提供稳定、高效的静态文件代理服务。

## ✨ 核心特性

* **高并发网络模型**：采用 `Epoll` I/O 多路复用技术（支持灵活配置 ET/LT 触发模式）结合线程池机制，实现主线程负责事件分发、工作线程负责业务逻辑的高效解耦。
* **HTTP/1.1 协议解析**：实现了 HTTP GET 请求的状态机解析，支持 `Keep-Alive` 长连接复用。
* **高效文件传输**：利用 `mmap` 将请求的静态文件映射到内存中，并结合 `writev` 系统调用实现分散写（Scatter/Gather I/O），大幅减少数据拷贝次数和上下文切换开销。
* **超时连接管理**：基于 **小根堆（Min-Heap）** 结构实现的高效定时器，利用 `epoll_wait` 的超时时间精准驱动，及时剔除空闲或异常断开的非活动连接，防止系统文件描述符耗尽。
* **异步日志系统**：基于生产者-消费者模型实现的单例异步日志模块，内部使用线程安全的阻塞队列缓存日志，由独立后台写线程负责刷盘，避免 I/O 阻塞业务线程。
* **自定义缓冲区**：封装了支持自动扩容的读写 Buffer，利用 `readv` 从套接字高效读取数据，有效处理底层的网络粘包与半包问题。

## 📁 目录结构

```text
WebServer/
├── src/
│   ├── socket/       # Reactor 核心网络模块 (Server, Epoller)
│   ├── http/         # HTTP 状态机解析与响应模块 (Request, Response, Connect)
│   ├── timer/        # 基于小根堆的超时连接管理模块 (HeapTimer)
│   ├── log/          # 异步日志系统 (Log, BlockQueue)
│   └── utils/        # 基础组件 (动态 Buffer，并发线程池 ThreadPool)
├── resources/        # Web 静态资源目录 (存放 HTML, CSS, JS 及错误页面)
│   ├── snake/        # 贪吃蛇小游戏静态网页
│   └── error/        # 400, 403, 404 等状态码页面
├── log_data/         # 日志文件输出目录 (运行时自动生成)
├── build/            # 编译输出及可执行文件运行目录
├── main.cpp          # 服务器程序入口
└── CMakeLists.txt    # CMake 构建脚本
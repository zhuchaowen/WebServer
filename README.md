WebServer/
├── src/
│   ├── socket/       # Reactor 核心模块 (Server, Epoller)
│   ├── http/         # HTTP 状态机解析与响应模块 (Request, Response, Connect)
│   ├── threadpool/   # 并发控制模块 (ThreadPool)
│   ├── timer/        # 超时连接剔除模块 (HeapTimer)
│   ├── log/          # 异步日志系统 (Log, BlockQueue)
│   └── utils/        # 基础工具组件 (动态 Buffer)
├── resources/        # Web 静态资源目录 (HTML, CSS, JS, 错误页面等)
│   ├── snake/        # 贪吃蛇游戏源码
│   └── error/        # 403, 404 等状态码页面
├── main.cpp          # 服务器入口文件
└── CMakeLists.txt    # CMake 构建脚本
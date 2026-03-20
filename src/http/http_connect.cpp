#include "http_connect.h"

// 静态成员变量初始化
int HttpConnect::epoll_fd = -1;
std::atomic<int> HttpConnect::user_count{0};
std::string HttpConnect::root_dir{};

void HttpConnect::init(int _fd, const sockaddr_in& _addr) 
{
    ++user_count;

    fd = _fd;
    addr = _addr;
    is_close = false;
    
    // 重置缓冲区，复用之前分配的内存
    read_buffer.retrieve_all();
    write_buffer.retrieve_all();
    
    // 重置解析器状态
    request.init();
}

void HttpConnect::close_connect() 
{
    // 只要关闭连接，必定执行 unmap_file，彻底堵死 mmap 内存泄漏的漏洞
    response.unmap_file(); 

    if (!is_close) {
        is_close = true;
        --user_count;

        close(fd);
        fd = -1;
    }
}

ssize_t HttpConnect::read(int* save_errno) 
{
    ssize_t len = -1;
    // 采用 ET (边缘触发) 模式，必须一次性将 socket 接收缓冲区中的数据读干净
    while (true) {
        // Buffer 内部使用了栈上的 64KB 临时数组和 readv，极大提升了读取效率
        len = read_buffer.read_fd(fd, save_errno);
        if (len <= 0) { 
            // 读到 0 说明对方关闭连接；< 0 且 errno 为 EAGAIN 说明缓冲区暂时读空了
            break; 
        } 
    }
    return len;
}

ssize_t HttpConnect::write(int* save_errno) 
{
    ssize_t len = -1;
    while (!is_close) {
        // 使用 writev 将响应头 (iov[0]) 和文件内容 (iov[1]) 集中发送
        len = writev(fd, iov, iov_count);
        if (len <= 0) {
            *save_errno = errno;
            break;
        }

        // 如果所有数据都已经发送完毕，退出循环
        if (iov[0].iov_len + iov[1].iov_len == 0) { 
            break; 
        } 
        
        // writev 返回的是本次总共发送的字节数 len，根据 len 手动调整 iov 的指针
        if (static_cast<size_t>(len) > iov[0].iov_len) {
            // 说明第一块 (响应头) 已经发完了，还发了一部分第二块 (文件内容)
            iov[1].iov_base = (uint8_t*)iov[1].iov_base + (len - iov[0].iov_len);
            iov[1].iov_len -= (len - iov[0].iov_len);
            
            if (iov[0].iov_len) {
                // 清空写缓冲区的已发数据
                write_buffer.retrieve_all();
                iov[0].iov_len = 0;
            }
        } else {
            // 说明第一块 (响应头) 都还没发完
            iov[0].iov_base = (uint8_t*)iov[0].iov_base + len;
            iov[0].iov_len -= len;
            write_buffer.retrieve(len);
        }
    }
    
    return len;
}

bool HttpConnect::process() 
{
    // 每次处理新请求前，先重置请求解析器
    request.init();
    
    // 缓冲区没数据，直接返回
    if (read_buffer.readable_bytes() <= 0) {
        return false;
    }
    
    // HttpRequest 进行协议解析
    if (request.parse(read_buffer)) {
        // 解析成功，初始化 HttpResponse 为 200 OK，并传入请求的资源路径
        response.init(root_dir, request.get_path(), request.is_keep_alive(), 200);
    } else {
        // 解析失败，初始化 HttpResponse 为 400 Bad Request
        response.init(root_dir, request.get_path(), false, 400);
    }
    
    // HttpResponse 生成响应报文 (状态行、响应头)，并将其追加到写缓冲区
    response.make_response(write_buffer);

    // 准备分散写的 iovec 结构体
    // 第一块：HTTP 响应头部 (存储在 write_buffer 中)
    iov[0].iov_base = const_cast<char*>(write_buffer.peek());
    iov[0].iov_len = write_buffer.readable_bytes();
    iov_count = 1;

    // 第二块：客户端请求的文件内容 (通过 mmap 映射到了内存中)
    if (response.get_file() && response.get_file_len() > 0) {
        iov[1].iov_base = response.get_file();
        iov[1].iov_len = response.get_file_len();
        // 有文件内容，准备两块数据
        iov_count = 2; 
    }
    
    return true;
}
#include "http_connect.h"
#include "log.h"

// 静态成员变量初始化
std::atomic<int> HttpConnect::user_count{0};
std::string HttpConnect::root_dir{};

void HttpConnect::init(int _fd, const sockaddr_in& _addr)
{
    ++user_count;

    fd = _fd;
    addr = _addr;
    is_close = false;

    // 线程安全地解析并缓存客户端 IP
    char ip_buf[INET_ADDRSTRLEN];
    if (inet_ntop(AF_INET, &_addr.sin_addr, ip_buf, sizeof(ip_buf))) {
        // 将局部数组中的字符串安全地拷贝给 std::string 成员
        client_ip = ip_buf;
    } else {
        // 以防万一解析失败，防止出现空字符串
        client_ip = "Unknown";
    }

    // 无条件执行重置操作
    reset();
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

void HttpConnect::reset()
{
    read_buffer.retrieve_all();
    write_buffer.retrieve_all();
    request.init();

    response.unmap_file();
    iov_count = 0;
    iov[0].iov_len = 0;
    iov[1].iov_len = 0;
}

ssize_t HttpConnect::read(int* save_errno)
{
    // 记录本次总共读了多少
    ssize_t read_bytes = 0;

    // 采用 ET (边缘触发) 模式，必须一次性将 socket 接收缓冲区中的数据读干净
    while (true) {
        // Buffer 内部使用了栈上的 64KB 临时数组和 readv，极大提升了读取效率
        ssize_t len = read_buffer.read_from_fd(fd, save_errno);
        if (len > 0) {
            read_bytes += len;
        } else if (len == 0) {
            // 对方关闭连接
            return 0;
        } else {
            if (*save_errno == EAGAIN || *save_errno == EWOULDBLOCK) {
                // 读干净了，这是正常现象，跳出循环
                break;
            }

            // 真正的出错了
            return -1;
        }
    }

    return read_bytes;
}

ssize_t HttpConnect::write(int* save_errno)
{
    // 记录本次总共写了多少
    ssize_t write_bytes = 0;

    while (!is_close) {
        // 先检查是否所有数据都已经发送完毕，如果发完直接退出
        if (is_write_complete()) {
            break;
        }

        // 使用 writev 将响应头 (iov[0]) 和文件内容 (iov[1]) 集中发送
        ssize_t len = writev(fd, iov, iov_count);
        if (len <= 0) {
            *save_errno = errno;
            if (*save_errno == EAGAIN || *save_errno == EWOULDBLOCK) {
                // 网卡缓冲区满了，发不出去了，等待下次 epoll 唤醒 (EPOLLOUT 事件)
                return write_bytes;
            }

            // 真正的发送错误（如对方强制断开 RST）
            return -1;
        }

        write_bytes += len;

        // writev 返回的是本次总共发送的字节数 len，根据 len 手动调整 iov 的指针
        if (static_cast<size_t>(len) > iov[0].iov_len) {
            // 说明第一块 (响应头) 已经发完了，还发了一部分第二块 (文件内容)
            iov[1].iov_base = static_cast<uint8_t *>(iov[1].iov_base) + (len - iov[0].iov_len);
            iov[1].iov_len -= (len - iov[0].iov_len);

            if (iov[0].iov_len) {
                // 清空写缓冲区的已发数据
                write_buffer.retrieve_all();
                iov[0].iov_len = 0;
            }
        } else {
            // 说明第一块 (响应头) 都还没发完
            iov[0].iov_base = static_cast<uint8_t *>(iov[0].iov_base) + len;
            iov[0].iov_len -= len;
            write_buffer.retrieve(len);
        }
    }

    return write_bytes;
}

bool HttpConnect::process()
{
    // 缓冲区没数据，直接返回
    if (read_buffer.readable_bytes() <= 0) {
        return false;
    }

    // 解析 HTTP 报文
    bool parse_success = request.parse(read_buffer);

    if (!parse_success) {
        // 格式错误，彻底失败
        LOG_WARNING("Request Parse Failed! fd: %d", fd);
        response.init(request.get_code(), false, "", root_dir);
    } else if (!request.is_finish()) {
        // parse_success == true 但还没达到 FINISH 状态
        // 说明这是个半包，需要等下一次 epoll 读事件，什么都不做，继续监听 EPOLLIN
        return false;
    } else {
        // 完整且成功地解析完毕
        LOG_DEBUG("Request OK! fd: %d, Method: %s, Path: %s", fd, request.get_method().c_str(), request.get_path().c_str());
        response.init(200, request.is_keep_alive(), request.get_path(), root_dir);
    }

    // HttpResponse 生成响应报文 (状态行、响应头)，并将其追加到写缓冲区
    response.make_response(write_buffer);

    // 准备分散写的 iovec 结构体
    // 第一块：HTTP 响应头部 (存储在 write_buffer 中)
    iov[0].iov_base = const_cast<char*>(write_buffer.begin_read());
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
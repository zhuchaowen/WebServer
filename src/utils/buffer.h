#ifndef WEBSERVER_BUFFER_H
#define WEBSERVER_BUFFER_H

#include <iostream>
#include <vector>
#include <string>
#include <atomic>
#include <sys/uio.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>

class Buffer {
private:
    // 底层使用 std::vector<char> 保证内存连续性
    std::vector<char> buffer;

    // 使用 atomic 保证在多线程环境下获取游标状态的安全性
    std::atomic<std::size_t> read_pos;  // 读游标
    std::atomic<std::size_t> write_pos; // 写游标

    // 获取底层 vector 数据块的绝对起始地址
    char* begin_ptr() noexcept { return &*buffer.begin(); }
    const char* begin_cptr() const noexcept { return &*buffer.begin(); }

    // 内部扩容或内存整理函数
    void make_space(size_t len);
public:
    // 默认初始分配 1024 字节，对于普通 HTTP 请求头足够了
    Buffer(int init_buff_size = 1024) : buffer(init_buff_size), read_pos(0), write_pos(0) {}

    // 容量与长度查询接口
    // 获取当前可以读取的字节数 (写指针 - 读指针)
    size_t readable_bytes() const noexcept { return write_pos - read_pos; }
    // 获取当前可以写入的字节数 (底层 vector 容量 - 写指针)
    size_t writable_bytes() const noexcept { return buffer.size() - write_pos; }
    // 获取前面已经读取过、可以被回收利用的预留空间 (读指针当前位置)
    size_t prependable_bytes() const noexcept { return read_pos; }

    // 读数据接口 (供 HTTP 状态机使用)
    // 获取当前待读取数据的起始内存指针
    const char* peek() const noexcept { return begin_cptr() + read_pos; }
    // 业务层读取了 len 长度的数据后，调用此函数向后移动读指针
    void retrieve(size_t len) noexcept;
    // 业务层一直读取到 end 指针所在的位置 (常用于按行读取 \r\n)
    void retrieve_until(const char* end) noexcept;
    // 数据全部读完，清空缓冲区 (重置读写指针到 0，复用内存)
    void retrieve_all() noexcept;
    // 将所有可读数据作为一个 std::string 返回，并清空缓冲区
    std::string retrieve_all_to_str();

    // 写数据接口 (供数据追加使用)
    // 获取当前可写区域的起始内存指针
    char* begin_write() noexcept { return begin_ptr() + write_pos; }
    const char* begin_write_const() const noexcept { return begin_cptr() + write_pos; }
    // 往缓冲区写入 len 长度数据后，调用此函数向后移动写指针
    void has_written(size_t len) noexcept { write_pos += len; }
    // 确保缓冲区有至少 len 字节的剩余空间，如果不够会自动扩容或整理内存
    void ensure_writable(size_t len);
    // 追加字符串或二进制块到缓冲区尾部
    void append(const std::string& str) { append(str.data(), str.length()); }
    void append(const char* str, size_t len);
    void append(const void* data, size_t len);

    // 核心网络 I/O 接口
    // 从套接字读取数据 (使用 readv 分散读，极大提升性能并防止溢出)
    ssize_t read_fd(int fd, int* save_errno) noexcept;
    // 将缓冲区中的可读数据写入到套接字中
    ssize_t write_fd(int fd, int* save_errno) noexcept;
};

#endif //WEBSERVER_BUFFER_H
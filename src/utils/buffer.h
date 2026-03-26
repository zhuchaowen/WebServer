#ifndef WEBSERVER_BUFFER_H
#define WEBSERVER_BUFFER_H

#include <vector>
#include <string>

class Buffer {
private:
    // 底层使用 std::vector<char> 保证内存连续性
    std::vector<char> buffer;

    size_t read_pos;    // 读游标
    size_t write_pos;   // 写游标

    // 栈上分配的额外空间
    static constexpr int STACK_BUFFER = 65536;

    // 获取底层 vector 数据块的绝对起始地址
    char* begin_ptr() noexcept { return &buffer[0]; }
    const char* const_begin_ptr() const noexcept { return &buffer[0]; }

    // 内存整理或内部扩容
    void make_space(size_t size);
public:
    // 默认初始分配 1024 字节，对于普通 HTTP 请求头足够了
    explicit Buffer(size_t size = 1024) : buffer(size), read_pos(0), write_pos(0) {}

    // 获取当前可以读取的字节数 (写指针 - 读指针)
    size_t readable_bytes() const noexcept { return write_pos - read_pos; }
    // 获取当前可以写入的字节数 (底层 vector 容量 - 写指针)
    size_t writable_bytes() const noexcept { return buffer.size() - write_pos; }
    // 获取前面已经读取过、可以被回收利用的预留空间 (读指针当前位置)
    size_t recyclable_bytes() const noexcept { return read_pos; }

    // 获取当前待读取数据的起始内存指针
    char* begin_read() noexcept { return begin_ptr() + read_pos; }
    const char* const_begin_read() const noexcept { return const_begin_ptr() + read_pos; }
    // 业务层读取了 size 字节的数据后，调用此函数向后移动读指针
    void retrieve(size_t size) noexcept;
    // 业务层一直读取到 end 指针所在的位置 (常用于按行读取 \r\n)
    void retrieve_until(const char* end) noexcept;
    // 数据全部读完，清空缓冲区 (重置读写指针到 0，复用内存)
    void retrieve_all() noexcept;
    // 将所有可读数据作为一个 std::string 返回，并清空缓冲区
    std::string retrieve_all_to_string();

    // 获取当前可写区域的起始内存指针
    char* begin_write() noexcept { return begin_ptr() + write_pos; }
    const char* const_begin_write() const noexcept { return const_begin_ptr() + write_pos; }
    // 往缓冲区写入 size 字节数据后，调用此函数向后移动写指针
    void have_written(size_t size) noexcept { write_pos += size; }
    // 确保缓冲区有至少 size 字节的剩余空间，如果不够会自动扩容或整理内存
    void ensure_writable(size_t size);
    // 追加字符串或二进制块到缓冲区尾部
    void append(const char* str, size_t len);
    void append(const void* data, size_t size);
    void append(const std::string& str);

    // 从套接字读取数据
    ssize_t read_from_fd(int fd, int* save_errno) noexcept;
    // 将缓冲区中的可读数据写入到套接字中
    ssize_t write_to_fd(int fd, int* save_errno) noexcept;
};


#endif //WEBSERVER_BUFFER_H
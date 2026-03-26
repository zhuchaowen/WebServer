#include <algorithm>
#include <unistd.h>
#include <sys/uio.h>
#include <cstring>
#include "buffer.h"

void Buffer::retrieve(size_t size) noexcept
{
    // 如果要求的长度超出可读，就只读到最大可读处
    if (size > readable_bytes()) {
        size = readable_bytes();
    }

    read_pos += size;
}

void Buffer::retrieve_until(const char* end) noexcept
{
    if (const_begin_read() < end) {
        // 移动读指针到 end 所在的位置
        retrieve(end - const_begin_read());
    }
}

void Buffer::retrieve_all() noexcept
{
    read_pos = 0;
    write_pos = 0;
}

std::string Buffer::retrieve_all_to_string()
{
    std::string str(begin_read(), begin_write());

    // 提取完后重置游标
    retrieve_all();

    return str;
}

void Buffer::make_space(size_t size)
{
    // 剩余可写空间 + 前面已读废弃的空间 < 想要写入的 size
    // 说明这块 vector 彻底不够用，必须找系统重新申请更大的内存
    if (writable_bytes() + recyclable_bytes() < size) {
        // 采用 1.5 倍扩容策略，而不是加多少扩多少
        size_t new_size = buffer.size() + std::max(buffer.size() / 2, size);
        buffer.resize(new_size);
    } else {
        // 容量其实是够的，只是前面的可读数据被取走后，前面空出了很大一块内部碎片
        // 把现存的未读数据整体往前挪动到 buffer 数组的头部，这样就给后半部分腾出了连续的大块空间
        size_t readable = readable_bytes();
        memmove(begin_ptr(), const_begin_read(), readable);

        // 重置游标
        read_pos = 0;
        write_pos = readable;
    }
}

void Buffer::ensure_writable(size_t size)
{
    // 如果不够写，触发内部空间整理或扩容
    if (writable_bytes() < size) {
        make_space(size);
    }
}

void Buffer::append(const char* str, size_t len)
{
    if (str) {
        // 确保容量足够
        ensure_writable(len);

        // 将数据拷贝到写指针所在的位置
        std::copy_n(str, len, begin_write());

        // 更新写指针
        have_written(len);
    }
}

void Buffer::append(const void* data, size_t size)
{
    if (data) {
        append(static_cast<const char*>(data), size);
    }
}

void Buffer::append(const std::string& str)
{
    if (!str.empty()) {
        append(str.c_str(), str.length());
    }
}

ssize_t Buffer::read_from_fd(int fd, int* save_errno) noexcept
{
    // 分散读取机制，一次尽可能多地读取数据，减少系统调用(read)的次数
    // 在栈上分配 STACK_SPACE 字节的额外空间，栈内存分配极快，退出作用域自动释放
    char buf[STACK_BUFFER];

    iovec iov[2];
    size_t writable = writable_bytes();

    // 第一块：指向 Buffer 现有的剩余空闲空间
    iov[0].iov_base = begin_write();
    iov[0].iov_len = writable;

    // 第二块：指向栈上的 STACK_SPACE 字节临时大数组
    iov[1].iov_base = buf;
    iov[1].iov_len = sizeof(buf);

    // 如果 Buffer 剩下的空间小于待读取的字节数，就同时使用这两块内存 (iov[0] 满了就会写到 iov[1])
    // 如果 Buffer 空间本身就足够，就没必要用栈空间了
    ssize_t len = readv(fd, iov, 2);

    if (len < 0) {
        *save_errno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        // 数据完全被现有的 Buffer 空间装下了，只需要向后移动写指针
        have_written(len);
    } else {
        // Buffer 原有的空间被塞满了，溢出的数据写到了栈上的 buff 数组里
        // 原空间写满，指针指到最后
        write_pos = buffer.size();

        // 将额外读到栈上的数据，安全地追加到 Buffer 中。
        // 这内部会自动调用 ensure_writable 和 make_space 进行 std::vector 的动态扩容
        append(buf, len - writable);
    }

    return len;
}

ssize_t Buffer::write_to_fd(int fd, int* save_errno) noexcept
{
    ssize_t len = write(fd, const_begin_write(),readable_bytes());
    if (len < 0) {
        *save_errno = errno;
    } else {
        // 数据成功发送给客户端后，向后移动读游标（相当于抛弃了已发送的数据）
        read_pos += len;
    }

    return len;
}
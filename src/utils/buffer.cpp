#include "buffer.h"
     
void Buffer::retrieve(size_t len) noexcept 
{
    // 确保业务层要求移动的长度不会超过实际可读的长度
    assert(len <= readable_bytes());
    read_pos += len;
}           
 
void Buffer::retrieve_until(const char* end) noexcept
{
    assert(peek() <= end);
    // 移动读指针到 end 所在的位置
    retrieve(end - peek());
}
 
void Buffer::retrieve_all() noexcept
{
    read_pos = 0;
    write_pos = 0;
}
     
std::string Buffer::retrieve_all_to_str()
{
    std::string str(peek(), readable_bytes());
    // 提取完后重置游标
    retrieve_all();
    return str;
}

void Buffer::append(const char* str, size_t len)
{
    assert(str);
    // 确保容量足够
    ensure_writable(len);
    // 将数据拷贝到写指针所在的位置
    std::copy(str, str + len, begin_write());
    // 更新写指针
    has_written(len);
}

void Buffer::append(const void* data, size_t len)
{
    assert(data);
    append(static_cast<const char*>(data), len);
}

void Buffer::ensure_writable(size_t len)
{
    if(writable_bytes() < len) {
        // 如果不够写，触发内部空间整理或扩容
        make_space(len); 
    }
    assert(writable_bytes() >= len);
}

ssize_t Buffer::read_fd(int fd, int* save_errno) noexcept
{
    // 核心：分散读取 (Scatter Read) 机制
    // 在栈上分配 STACK_SPACE 字节的额外空间。栈内存分配极快，退出作用域自动释放
    // 目的：一次尽可能多地读取数据，减少系统调用(read)的次数
    char buff[STACK_SPACE];

    iovec iov[2];
    const size_t writable = writable_bytes();
    
    // 第一块：指向 Buffer 现有的剩余空闲空间
    iov[0].iov_base = begin_write();
    iov[0].iov_len = writable;
    
    // 第二块：指向栈上的 STACK_SPACE 字节临时大数组
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    // 如果 Buffer 剩下的空间小于 STACK_SPACE 字节，就同时使用这两块内存 (iov[0] 满了就会写到 iov[1])
    // 如果 Buffer 空间本身就很大，就没必要用栈空间了
    const ssize_t len = readv(fd, iov, 2);
    
    if (len < 0) {
        *save_errno = errno;
    } else if (static_cast<size_t>(len) <= writable) {
        // 数据完全被现有的 Buffer 空间装下了，只需要向后移动写指针
        write_pos += len;
    } else {
        // Buffer 原有的空间被塞满了，溢出的数据写到了栈上的 buff 数组里
        // 原空间写满，指针指到最后
        write_pos = buffer.size(); 
        
        // 将额外读到栈上的数据，安全地追加到 Buffer 中。
        // 这内部会自动调用 ensure_writable 和 make_space 进行 std::vector 的动态扩容
        append(buff, len - writable);
    }
    return len;
}

ssize_t Buffer::write_fd(int fd, int* save_errno) noexcept
{
    ssize_t len = write(fd, peek(), readable_bytes());
    if (len < 0) {
        *save_errno = errno;
        return len;
    } 
    // 数据成功发送给客户端后，向后移动读游标（相当于抛弃了已发送的数据）
    read_pos += len;
    return len;
}

void Buffer::make_space(size_t len) 
{
    // 剩余可写空间 + 前面已读废弃的空间 < 想要写入的 len
    // 说明这块 vector 彻底不够用，必须找系统重新申请更大的内存
    if (writable_bytes() + prependable_bytes() < len) {
        // resize 会分配新内存，并把老数据拷过去
        buffer.resize(write_pos + len + 1);
    } else {
        // 容量其实是够的，只是前面的可读数据被取走后，前面空出了很大一块（内部碎片）
        // 把现存的未读数据整体往前挪动到 buffer 数组的头部 index = 0 处，这样就给后半部分腾出了连续的大块空间
        size_t readable = readable_bytes();
        std::copy(begin_ptr() + read_pos, begin_ptr() + write_pos, begin_ptr());
        
        // 重置游标
        read_pos = 0;
        write_pos = read_pos + readable;
        
        // 确保移动后可读数据长度不变
        assert(readable == readable_bytes());
    }
}
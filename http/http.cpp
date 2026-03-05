#include "http.h"
#include <cerrno>
#include <cstring>

int http::epoll_fd = -1;
int http::user_count = 0;

// 初始化连接
void http::init(int _fd)
{
    fd = _fd;

    // 设置端口复用
    int reuse = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // 添加到epoll对象中
    addfd(epoll_fd, fd, true);

    ++user_count;

    init();
}

// 关闭连接
void http::close_connection()
{
    if (fd != -1) {
        delfd(epoll_fd, fd);
        fd = -1;
        --user_count;
    }
}

// 循环读取数据，直到无数据可读或对方关闭连接
bool http::read()
{
    if (read_index >= BUFFER_SIZE) {
        return false;
    }

    int bytes = 0;
    while (true) {
        // 从read_buf + read_index索引处开始保存数据，大小是BUFFER_SIZE - read_index
        bytes = recv(fd, read_buf + read_index, BUFFER_SIZE - read_index, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 没有数据
                break;
            }
            return false;
        } else if (bytes == 0) {
            // 对方关闭连接
            return false;
        } 

        read_index += bytes;
    } 

    return true;
} 

// 写HTTP响应
bool http::write()
{
    int temp = 0;
    
    if (bytes_to_send == 0) {
        // 将要发送的字节为0，这一次响应结束
        modfd(epoll_fd, fd, EPOLLIN); 
        init();
        return true;
    }

    // 一次可能发不完，需要多次发送
    while(true) {
        // 分散写：同时发送响应头和文件内容
        temp = writev(fd, iv, iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
            // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
            if(errno == EAGAIN) {
                modfd(epoll_fd, fd, EPOLLOUT);
                return true;
            }

            unmap();
            return false;
        }

        // 部分发送成功
        bytes_have_send += temp;    // 累计已发送字节数
        bytes_to_send -= temp;      // 剩余待发送字节数

        if (bytes_have_send >= iv[0].iov_len) {
            // 响应头已发完，开始发文件
            iv[0].iov_len = 0;  

            // 调整文件起始位置，跳过已发送的部分
            iv[1].iov_base = file_address + (bytes_have_send - write_index);
            // 剩余文件大小
            iv[1].iov_len = bytes_to_send;
        } else {
            // 响应头还没发完
            // 只调整响应头的起始位置和剩余长度
            iv[0].iov_base = write_buf + bytes_have_send;
            iv[0].iov_len = iv[0].iov_len - temp;
        }

        if (bytes_to_send <= 0) {
            // 没有数据要发送了
            unmap();
            modfd(epoll_fd, fd, EPOLLIN);

            if (keep_alive) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }

    return true;
}                                         

// 由线程池中的工作线程调用，这是处理http请求的入口函数
void http::process()
{
    // 解析http请求
    HTTP_CODE read_ret = process_read();
    if (read_ret == HTTP_CODE::NO_REQUEST) {
        modfd(epoll_fd, fd, EPOLLIN);
        return;
    }

    // 生成响应
    if (!process_write(read_ret)) {
        close_connection();
    }
    modfd(epoll_fd, fd, EPOLLOUT);
}

void http::init()
{
    method = METHOD::GET;
    url = nullptr;
    version = nullptr;
    host = nullptr;
    keep_alive = false;
    content_length = 0;

    read_index = 0;
    checked_index = 0;
    line_start = 0;
    check_state = CHECK_STATE::REQUESTLINE; // 初始化状态为解析请求行

    write_index = 0;
    file_address = nullptr;
    iv_count = 0;
    bytes_to_send = 0;
    bytes_have_send = 0;

    bzero(real_file, FILENAME_LEN);
    bzero(read_buf, BUFFER_SIZE);
    bzero(write_buf, BUFFER_SIZE);
}

// 主状态机，解析请求
HTTP_CODE http::process_read()
{
    HTTP_CODE http_code = HTTP_CODE::NO_REQUEST;
    LINE_STATUS line_status = LINE_STATUS::LINE_OK;

    char* text = nullptr;
    while ((check_state == CHECK_STATE::CONTENT && line_status == LINE_STATUS::LINE_OK)
        || (line_status = parse_line()) == LINE_STATUS::LINE_OK) {
        // 解析到了一行完整的数据，或者解析到了请求体（也是完整的数据）
        
        // 获取一行数据
        text = get_line();
        line_start = checked_index;

        switch (check_state) {
            case CHECK_STATE::REQUESTLINE:
                http_code = parse_request_line(text);
                if (http_code == HTTP_CODE::BAD_REQUEST) {
                    return HTTP_CODE::BAD_REQUEST;
                }
                break;
            case CHECK_STATE::HEADER:
                http_code = parse_header(text);
                if (http_code == HTTP_CODE::BAD_REQUEST) {
                    return HTTP_CODE::BAD_REQUEST;
                } else if (http_code == HTTP_CODE::GET_REQUEST) {
                    return do_request();
                }
                break;
            case CHECK_STATE::CONTENT:
                http_code = parse_content(text);
                if (http_code == HTTP_CODE::GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_STATUS::LINE_OPEN;
                break;
            default:
                return HTTP_CODE::INTERNAL_ERROR;
                break;
        }
    }

    return HTTP_CODE::NO_REQUEST;
}

// 根据服务器处理http请求的结果，决定返回给客户端的内容
bool http::process_write(HTTP_CODE read_ret)
{
    switch (read_ret) {
        case HTTP_CODE::INTERNAL_ERROR:
            add_status_line(500, ERROR_500_TITLE);
            add_headers(strlen(ERROR_500_FORM));
            if (!add_content(ERROR_500_FORM)) {
                return false;
            }
            break;
        case HTTP_CODE::BAD_REQUEST:
            add_status_line(400, ERROR_400_TITLE);
            add_headers(strlen(ERROR_400_FORM));
            if (!add_content(ERROR_400_FORM)) {
                return false;
            }
            break;
        case HTTP_CODE::NO_RESOURCE:
            add_status_line(404, ERROR_404_TITLE);
            add_headers(strlen(ERROR_404_FORM));
            if (!add_content(ERROR_404_FORM)) {
                return false;
            }
            break;
        case HTTP_CODE::FORBIDDEN_REQUEST:
            add_status_line(403, ERROR_403_TITLE);
            add_headers(strlen(ERROR_403_FORM));
            if (!add_content(ERROR_403_FORM)) {
                return false;
            }
            break;
        case HTTP_CODE::FILE_REQUEST:
            add_status_line(200, OK_200_TITLE);
            add_headers(file_stat.st_size);

            // 使用writev分散I/O，准备发送两部分数据
            // 第一部分：HTTP响应头（已缓存在write_buf中） 
            iv[0].iov_base = write_buf;           
            // 响应头的长度
            iv[0].iov_len = write_index;        

            // 第二部分：请求的文件内容（通过mmap映射）
            iv[1].iov_base = file_address; 
            // 文件的大小     
            iv[1].iov_len = file_stat.st_size;  

            // 有两个iovec结构，即有两块数据要发送
            iv_count = 2;                       

            // 总共要发送的字节数
            bytes_to_send = write_index + file_stat.st_size;

            return true;
        default:
            return false;
    }

    // 对于所有错误响应，只需要发送write_buf中的内容
    // 指向写缓冲区
    iv[0].iov_base = write_buf;
    // 缓冲区中已写入的数据长度
    iv[0].iov_len = write_index;
    // 只有一块数据
    iv_count = 1;
    // 需要发送的总字节数
    bytes_to_send = write_index;

    return true;
}

// 解析http请求行，获得请求方法，目标url，http版本
HTTP_CODE http::parse_request_line(char* text)
{
    // GET /index.html HTTP/1.1
    url = strpbrk(text, " \t");
    // GET\0/index.html HTTP/1.1
    *url++ = '\0';

    char* m = text;
    if (strcasecmp(m, "GET") == 0) {
        method = METHOD::GET;
    } else {
        return HTTP_CODE::BAD_REQUEST;
    }

    // /index.html HTTP/1.1
    version = strpbrk(url, " \t");
    // /index.html\0HTTP/1.1
    *version++ = '\0';

    // http://192.168.52.128:10000/index.html
    if (strncasecmp(url, "http://", 7) == 0) {
        url += 7;               // 192.168.52.128:10000/index.html
        url = strchr(url, '/'); // /index.html
    }

    if (!url || url[0] != '/') {
        return HTTP_CODE::BAD_REQUEST;
    }

    // 主状态机状态变为检查请求头
    check_state = CHECK_STATE::HEADER;
    
    return HTTP_CODE::NO_REQUEST;
}

// 解析http请求的头部信息
HTTP_CODE http::parse_header(char* text)
{
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取content_length字节的消息体，
        // 状态机转移到CHECK_STATE::CONTENT状态
        if (content_length) {
            check_state = CHECK_STATE::CONTENT;
            return HTTP_CODE::NO_REQUEST;
        }
        // 得到了一个完整的http请求
        return HTTP_CODE::GET_REQUEST;
    }

    if (strncasecmp(text, "Connection:", 11 ) == 0) {
        // 处理Connection头部字段  Connection: keep-alive
        text += 11;
        text += strspn( text, " \t" );
        if (strcasecmp(text, "keep-alive") == 0) {
            keep_alive = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, " \t");
        content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        // 处理Host头部字段
        text += 5;
        text += strspn( text, " \t" );
        host = text;
    } 

    return HTTP_CODE::NO_REQUEST;
}

// 没有真正解析http请求的消息体，只是判断它是否被完整的读入了
HTTP_CODE http::parse_content(char* text)
{
    if (read_index >= content_length + checked_index) {
        text[content_length] = '\0';
        return HTTP_CODE::GET_REQUEST;
    }

    return HTTP_CODE::NO_REQUEST;
}

// 当得到一个完整、正确的http请求时，分析目标文件的属性
// 如果目标文件存在、对所有用户可读，且不是目录，则使用mmap将其映射到内存地址file_address处，并告诉调用者获取文件成功
HTTP_CODE http::do_request()
{
    // "/home/zhu/WebServer/resources"
    strcpy(real_file, DOC_ROOT);
    int len = strlen(DOC_ROOT);
    // "/home/zhu/WebServer/resources/index.html"
    strncpy(real_file + len, url, FILENAME_LEN - len - 1);

    // 获取real_file文件的相关的状态信息，-1失败，0成功
    if (stat(real_file, &file_stat) < 0) {
        return HTTP_CODE::NO_RESOURCE;
    }

    // 判断访问权限
    if (!(file_stat.st_mode & S_IROTH)) {
        return HTTP_CODE::FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if (S_ISDIR(file_stat.st_mode)) {
        return HTTP_CODE::BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open(real_file, O_RDONLY);
    // 创建内存映射
    file_address = (char*)mmap(0, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return HTTP_CODE::FILE_REQUEST;
}

// 解析一行，判断依据 \r\n
LINE_STATUS http::parse_line()
{
    char temp;
    while (checked_index < read_index) {
        temp = read_buf[checked_index];
        if (temp == '\r') {
            if (checked_index + 1 == read_index) {
                return LINE_STATUS::LINE_OPEN;
            } else if (read_buf[checked_index + 1] == '\n') {
                read_buf[checked_index++] = '\0';
                read_buf[checked_index++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        } else if (temp == '\n') {
            if (checked_index > 1 && read_buf[checked_index - 1] == '\r') {
                read_buf[checked_index - 1] = '\0';
                read_buf[checked_index++] = '\0';
                return LINE_STATUS::LINE_OK;
            }
            return LINE_STATUS::LINE_BAD;
        }

        ++checked_index;
    }

    return LINE_STATUS::LINE_OPEN;
}
             
// 对内存映射区执行munmap操作
void http::unmap()
{
    if(file_address) {
        munmap(file_address, file_stat.st_size);
        file_address = nullptr;
    }
}

// 往写缓冲中写入待发送的数据
bool http::add_response(const char* format, ...)
{
    // 检查缓冲区是否已满，防止越界写入
    if(write_index >= BUFFER_SIZE) {
        return false;
    }

    va_list arg_list;           // 声明可变参数列表变量
    va_start(arg_list, format); // 初始化参数列表，指向第一个可变参数

    int len = vsnprintf(write_buf + write_index, BUFFER_SIZE - 1 - write_index, format, arg_list);
    // 缓冲区无法容纳完整的格式化结果
    if(len >= BUFFER_SIZE - 1 - write_index) {
        return false;
    }

    write_index += len; // 更新写位置索引
    va_end(arg_list);   // 清理可变参数列表占用的资源

    return true;
}

bool http::add_status_line(int status, const char* title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}   

bool http::add_headers(int length)
{
    return add_content_length(length) && add_content_type() && add_connection() && add_blank_line();
}

bool http::add_content_length(int length)
{
    return add_response("Content-Length: %d\r\n", length);
}

bool http::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

bool http::add_connection()
{
    return add_response("Connection: %s\r\n", keep_alive ? "keep-alive" : "close");
}

bool http::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http::add_content(const char* content)
{
    return add_response("%s", content);
}
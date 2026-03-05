#pragma once
#include "util.h"
#include "common.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/uio.h>

class http {
private:
    int fd;                                     // http连接的socket

    METHOD method;                              // http方法
    char* url;                                  // 请求目标文件的文件名
    char* version;                              // 协议版本
    char* host;                                 // 主机名
    bool keep_alive;                            // http请求是否要保持连接
    int content_length;                         // http请求的消息总长度
    char real_file[FILENAME_LEN];               // 客户请求的目标文件的完整路径，其内容等于DOC_ROOT + url，DOC_ROOT是网站根目录

    char read_buf[BUFFER_SIZE];                 // 读缓冲区
    int read_index;                             // 标识读缓冲区中已经读入的客户端数据的最后一个字节的下一个位置
    int checked_index;                          // 当前正在分析的字符在读缓冲区中的位置
    int line_start;                             // 当前正在解析的行的起始位置
    CHECK_STATE check_state;                    // 主状态机当前所处的状态

    char write_buf[BUFFER_SIZE];                // 写缓冲区
    int write_index;                            // 写缓冲区中待发送的字节数
    char* file_address;                         // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat file_stat;                      // 目标文件的状态。通过它可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    iovec iv[2];                                // 采用writev来执行写操作
    int iv_count;                               // 被写内存块的数量
    int bytes_to_send;                          // 将要发送的数据的字节数
    int bytes_have_send;                        // 已经发送的字节数
    
    void init();                                // 初始化信息
    HTTP_CODE process_read();                   // 解析http请求
    bool process_write(HTTP_CODE read_ret);     // 填充http应答

    // 这一组函数被process_read调用以解析HTTP请求
    HTTP_CODE parse_request_line(char* text);   
    HTTP_CODE parse_header(char* text);         
    HTTP_CODE parse_content(char* text);        
    HTTP_CODE do_request();                      
    LINE_STATUS parse_line();
    char* get_line() { return read_buf + line_start; }

    // 这一组函数被process_write调用以填充HTTP应答。
    void unmap();
    bool add_response(const char* format, ...);
    bool add_status_line(int status, const char* title);
    bool add_headers(int length );
    bool add_content_length(int length);
    bool add_content_type();
    bool add_connection();
    bool add_blank_line();
    bool add_content(const char* content);
public:
    static int epoll_fd;                        // 所有socket上的事件都被注册到同一个epoll内核事件中
    static int user_count;                      // 统计用户的数量

    http(){}
    ~http(){}

    void init(int _fd);                         // 初始化连接
    void close_connection();                    // 关闭连接    
    bool read();                                // 非阻塞地读                   
    bool write();                               // 非阻塞地写                                      
    void process();                             // 处理客户端请求                               
};
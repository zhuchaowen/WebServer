#pragma once

// 全局配置 
constexpr int MAX_FD = 4096;           // 最大的文件描述符个数      
constexpr int MAX_EVENT_NUMBER = 8192; // 监听的最大事件数量
constexpr int BUFFER_SIZE = 4096;       // 缓冲区的大小   
constexpr int FILENAME_LEN = 256;       // 文件名的最大长度       

// HTTP响应的一些状态信息
constexpr const char* OK_200_TITLE    = "OK";
constexpr const char* ERROR_400_TITLE = "Bad Request";
constexpr const char* ERROR_400_FORM  = "Your request has bad syntax or is inherently impossible to satisfy.\n";
constexpr const char* ERROR_403_TITLE = "Forbidden";
constexpr const char* ERROR_403_FORM  = "You do not have permission to get file from this server.\n";
constexpr const char* ERROR_404_TITLE = "Not Found";
constexpr const char* ERROR_404_FORM  = "The requested file was not found on this server.\n";
constexpr const char* ERROR_500_TITLE = "Internal Error";
constexpr const char* ERROR_500_FORM  = "There was an unusual problem serving the requested file.\n";

// 网站的根目录
constexpr const char* DOC_ROOT = "/home/zhu/WebServer/resources";

// HTTP 方法
enum class METHOD {
    GET = 0,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT
};

// 服务器处理HTTP请求的可能结果，报文解析的结果
enum class HTTP_CODE {
    NO_REQUEST,         // 请求不完整，需要继续读取客户数据
    GET_REQUEST,        // 表示获得了一个完成的客户请求
    BAD_REQUEST,        // 表示客户请求语法错误
    NO_RESOURCE,        // 表示服务器没有资源
    FORBIDDEN_REQUEST,  // 表示客户对资源没有足够的访问权限
    FILE_REQUEST,       // 文件请求,获取文件成功
    INTERNAL_ERROR,     // 表示服务器内部错误
    CLOSED_CONNECTION   // 表示客户端已经关闭连接了
};

// 解析客户端请求时，主状态机的状态
enum class CHECK_STATE {
    REQUESTLINE = 0,    // 当前正在解析请求行
    HEADER,             // 当前正在解析头部字段
    CONTENT             // 当前正在解析请求体
};

// 从状态机的三种可能状态，行的读取状态
enum class LINE_STATUS {
    LINE_OK = 0,    // 读取到一个完整的行
    LINE_BAD,       // 行出错
    LINE_OPEN       // 行数据尚且不完整
};
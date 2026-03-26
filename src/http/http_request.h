#ifndef WEBSERVER_HTTP_REQUEST_H
#define WEBSERVER_HTTP_REQUEST_H

#include <string>
#include <unordered_map>
#include "buffer.h"

// HTTP 请求解析的主状态机状态
enum class PARSE_STATE {
    REQUEST_LINE,   // 正在解析请求行
    HEADERS,        // 正在解析请求头
    BODY,           // 正在解析请求体
    FINISH          // 解析完成
};

// HTTP 请求解析类
// 采用有限状态机模式，从 Buffer 中非阻塞地解析 HTTP 报文
class HttpRequest {
private:
    PARSE_STATE state;                                      // 状态机当前所处的状态
    int code;                                               // 记录解析过程中的状态码 (默认 200)

    std::string method;                                     // 请求方法 (GET, POST)
    std::string path;                                       // 请求路径 (/index.html)
    std::string version;                                    // 协议版本 (HTTP/1.1)
    size_t content_length;                                  // 缓存 Body 的总长度
    std::string body;                                       // 请求体内容

    std::unordered_map<std::string, std::string> headers;   // 存储所有的请求头字段
    std::unordered_map<std::string, std::string> post;      // 存储解析后的表单数据

    // 解析请求行 (包含 method, path, version)
    bool parse_request_line(const std::string& line);
    // 解析单个请求头字段 (Key: Value)
    bool parse_header(const std::string& line);
    // 解析请求体 (参数不应是按 \r\n 截取的 line，而应该是整块连续数据)
    void parse_body(const std::string& data);
    // 对解析出来的 path 进行进一步处理
    bool parse_path();
    // 将 body 中的表单字符串解析并填充到 post 字典中
    void parse_post();
public:
    HttpRequest() { init(); }

    // 初始化/重置解析器状态
    // 当一个长连接处理完一个请求，准备接收下一个请求时调用
    void init() noexcept;

    // 核心解析函数：从 Buffer 缓冲区中读取并解析 HTTP 报文
    bool parse(Buffer& buffer);

    //属性获取接口
    int get_code() const noexcept { return code; }
    const std::string& get_method() const noexcept { return method; }
    std::string& get_path() noexcept { return path; }
    const std::string& get_path() const noexcept { return path; }
    const std::string& get_version() const noexcept { return version; }

    // 获取指定的请求头字段值
    std::string get_header(const std::string& key) const;
    // 获取 POST 表单中的字段值
    std::string get_post(const std::string& key) const;

    // 提供给外部判断状态机是否走到了尽头
    bool is_finish() const noexcept { return state == PARSE_STATE::FINISH; }
    // 判断是否是长连接 (Keep-Alive)
    bool is_keep_alive() const noexcept;
};


#endif //WEBSERVER_HTTP_REQUEST_H
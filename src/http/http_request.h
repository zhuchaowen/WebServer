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

class HttpRequest {
private:
    // 当前解析状态
    PARSE_STATE state;

    // 请求行与请求体数据
    std::string method;
    std::string path;
    std::string version;
    std::string body;

    // 存储请求头键值对
    std::unordered_map<std::string, std::string> headers;

    // 存储 POST 表单键值对
    std::unordered_map<std::string, std::string> post;

    // 解析具体部分的内部函数
    bool parse_request_line(const std::string& line);
    void parse_header(const std::string& line);
    void parse_body(const std::string& line);

    // 路径处理
    void parse_path();

    // 表单解析
    void parse_post();
public:
    HttpRequest() { init(); }

    // 初始化/重置解析器状态
    void init() noexcept;

    // 核心解析函数：从 Buffer 缓冲区中读取并解析 HTTP 报文
    bool parse(Buffer& buff);

    // 状态查询接口
    const std::string& get_method() const noexcept { return method; }
    std::string& get_path() noexcept { return path; }
    const std::string& get_path() const noexcept { return path; }
    const std::string& get_version() const noexcept { return version; }
    // 提供给外部判断状态机是否走到了尽头
    bool is_finish() const { return state == PARSE_STATE::FINISH; }

    // 判断是否是长连接 (Keep-Alive)
    bool is_keep_alive() const;

    // 获取 POST 请求解析后的表单数据 (如果找不到返回空字符串，这里按值返回)
    std::string get_post(const std::string& key) const;
};

#endif //WEBSERVER_HTTP_REQUEST_H
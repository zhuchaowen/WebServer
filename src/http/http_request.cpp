#include "http_request.h"
#include <algorithm>

void HttpRequest::init() noexcept 
{
    state = PARSE_STATE::REQUEST_LINE;
    code = 200;

    method.clear();
    path.clear();
    version.clear();
    content_length = 0;
    body.clear();

    headers.clear();
    post.clear();
}

bool HttpRequest::parse(Buffer& buffer) 
{
    // 没数据不代表出错，只是没活干，返回 true 等待下次 epoll 唤醒
    if (buffer.readable_bytes() <= 0) {
        return true;
    }

    // HTTP 报文的换行符规范
    constexpr char CRLF[] = "\r\n";

    // 只要状态机没有走到 FINISH，且缓冲区里还有数据，就持续解析
    while (!is_finish() && buffer.readable_bytes()) {
        bool is_body = (state == PARSE_STATE::BODY);
        
        if (is_body) {
            // 如果因为某种原因进入了 BODY 但长度为 0，立刻结束
            if (content_length <= body.size()) {
                state = PARSE_STATE::FINISH;
                break;
            }

            // 只从 Buffer 中读取缺少的字节数，防止误吞下一次的 HTTP 请求（防 TCP 粘包）
            size_t read_size = std::min(content_length - body.size(), buffer.readable_bytes());
            std::string data(buffer.const_begin_read(), read_size);

            // 游标向后移动实际读取的长度
            buffer.retrieve(read_size);

            // parse_body 里拼接完判断是否达到 content_len，如果达到了要改成 FINISH 状态
            parse_body(data);
        } else {
            // 解析 Header 和 Request Line 时，寻找 \r\n
            const char* line_end = std::search(buffer.const_begin_read(), buffer.const_begin_write(), CRLF, CRLF + 2);

            // 没找到 \r\n，说明头部的半个包还没到齐，提前退出等待下次网络读取
            if (line_end == buffer.const_begin_write()) {
                break; 
            }

            std::string line = std::string(buffer.const_begin_read(), line_end);

            // 移动游标跳过当前行和 \r\n
            buffer.retrieve_until(line_end + 2);

            switch (state) {
                case PARSE_STATE::REQUEST_LINE:
                    if (!parse_request_line(line) || !parse_path()) {
                        // 如果解析报错（如 400, 403, 501），直接中断返回
                        return false;
                    }
                    break;
                case PARSE_STATE::HEADERS:
                    if (!parse_header(line)) {
                        // 比如遇到恶意的 Content-Length，返回 false
                        return false;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    
    return true;
}

bool HttpRequest::parse_request_line(const std::string& line)
{
    // 找到第一个空格（分隔 method 和 path）
    size_t pos1 = line.find(' ');
    // 找到第二个空格（分隔 path 和 version）
    size_t pos2 = line.find(' ', pos1 + 1);

    if (pos1 == std::string::npos || pos2 == std::string::npos) {
        // 格式彻底乱套，Bad Request
        code = 400;
        return false;
    }

    method = line.substr(0, pos1);
    // 只允许 GET 和 POST
    if (method != "GET" && method != "POST") {
        // Not Implemented
        code = 501;
        return false;
    }

    path = line.substr(pos1 + 1, pos2 - pos1 - 1);

    // 解析 HTTP 版本号 (跳过 "HTTP/")
    size_t http_pos = line.find("HTTP/", pos2);
    if (http_pos == std::string::npos) {
        // 找不到 HTTP 标识，Bad Request
        code = 400;
        return false;
    }

    version = line.substr(http_pos + 5);

    state = PARSE_STATE::HEADERS;

    return true;
}

bool HttpRequest::parse_header(const std::string& line)
{
    // 如果是空行，说明头部解析完毕，接下来是 Body
    if (line.empty()) {
        // 根据是否有 Content-Length 或请求方法决定是否需要解析 Body
        if (headers.contains("Content-Length")) {
            try {
                if ((content_length = std::stoi(headers["Content-Length"])) > 0) {
                    state = PARSE_STATE::BODY;
                    return true;
                }
            } catch (...) {
                // Content-Length 传了乱七八糟的字符
                code = 400;
                return false;
            }
        }

        // GET 或 没有 Body 的请求，直接完成
        state = PARSE_STATE::FINISH;

        return true;
    }

    // 查找冒号分隔符
    size_t pos = line.find(':');
    if (pos != std::string::npos) {
        std::string key = line.substr(0, pos);

        // 跳过冒号后面的前导空格
        size_t value_start = pos + 1;
        while (value_start < line.size() && (line[value_start] == ' ' || line[value_start] == '\t')) {
            value_start++;
        }

        headers[key] = line.substr(value_start);
    }

    return true;
}

void HttpRequest::parse_body(const std::string& data)
{
    // 采用追加模式，防止 Body 被 TCP 切割成了多段
    body += data;

    // 只有收集齐了 Content-Length 指定的数据大小，才算真正的完成
    if (body.size() >= content_length) {
        parse_post();
        state = PARSE_STATE::FINISH;
    }
}

bool HttpRequest::parse_path()
{
    // 如果用户直接访问根目录，默认重定向到主页
    if (path == "/") {
        path = "/index.html";
    } else {
        // 只要出现跨目录企图，直接定性为恶意越权访问
        if (path.find("../") != std::string::npos) {
            // Forbidden
            code = 403;
            return false;
        }
    }

    return true;
}

void HttpRequest::parse_post()
{
    // 目前仅支持 application/x-www-form-urlencoded 格式的 POST 表单解析
    if (method == "POST" && headers["Content-Type"] == "application/x-www-form-urlencoded") {
        if(body.empty()) {
            return;
        }

        // 解析类似于 key1=value1&key2=value2 的字符串
        int n = body.size();
        int i = 0, j = 0;
        std::string key, value;

        for (; i < n; i++) {
            char ch = body[i];
            if (ch == '=') {
                key = body.substr(j, i - j);
                j = i + 1;
            } else if (ch == '&') {
                value = body.substr(j, i - j);
                j = i + 1;
                post[key] = value;
            }
        }

        // 处理最后一对键值
        if (j < n) {
            value = body.substr(j, i - j);
            post[key] = value;
        }
    }
}

std::string HttpRequest::get_header(const std::string& key) const
{
    if (headers.contains(key)) {
        return headers.at(key);
    }

    return "";
}

std::string HttpRequest::get_post(const std::string& key) const
{
    if (post.contains(key)) {
        return post.at(key);
    }

    return "";
}

bool HttpRequest::is_keep_alive() const noexcept
{
    if (headers.contains("Connection")) {
        std::string conn = headers.at("Connection");
        if (conn == "close" || conn == "Close") {
            return false;
        }

        if (conn == "keep-alive" || conn == "Keep-Alive") {
            return true;
        }
    }

    // 如果没有 Connection 字段，只要是 HTTP/1.1，默认就是长连接
    return version == "1.1";
}
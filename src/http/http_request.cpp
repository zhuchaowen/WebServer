#include "http_request.h"

void HttpRequest::init() noexcept 
{
    state = PARSE_STATE::REQUEST_LINE;

    method.clear();
    path.clear();
    version.clear();
    body.clear();

    headers.clear();
    post.clear();
}

bool HttpRequest::parse(Buffer& buff) 
{
    // HTTP 报文的换行符规范
    constexpr char CRLF[] = "\r\n";

    if (buff.readable_bytes() <= 0) {
        return false;
    }

    // 只要状态机没有走到 FINISH，且缓冲区里还有数据，就持续解析
    while (state != PARSE_STATE::FINISH && buff.readable_bytes()) {
        std::string line;
        bool is_body = (state == PARSE_STATE::BODY);
        
        if (is_body) {
            // 计算当前还缺多少字节的 Body
            size_t content_len = 0;
            if (headers.count("Content-Length")) {
                content_len = std::stoi(headers["Content-Length"]);
            }
            // 只从 Buffer 中读取缺少的字节数，防止误吞下一次的 HTTP 请求（防 TCP 粘包）
            size_t read_size = std::min(content_len - body.size(), buff.readable_bytes());
            
            line = std::string(buff.peek(), read_size);
            buff.retrieve(read_size); // 游标向后移动实际读取的长度
        } else {
            // 解析 Header 和 Request Line 时，寻找 \r\n
            const char* line_end = std::search(buff.peek(), buff.begin_write_const(), CRLF, CRLF + 2);
            if (line_end == buff.begin_write_const()) {
                // 没找到 \r\n，说明头部的半个包还没到齐，提前退出等待下次网络读取
                break; 
            }
            line = std::string(buff.peek(), line_end);
            buff.retrieve_until(line_end + 2); // 移动游标跳过当前行和 \r\n
        }

        switch (state) {
            case PARSE_STATE::REQUEST_LINE:
                if (!parse_request_line(line)) {
                    // 请求行格式错误
                    return false; 
                }
                // 处理请求路径（例如将 "/" 转换为 "/index.html"）
                parse_path(); 
                break;
            case PARSE_STATE::HEADERS:
                parse_header(line);
                break;
            case PARSE_STATE::BODY:
                parse_body(line);
                break;  
            default:
                break;
        }
    }

    return true;
}

bool HttpRequest::parse_request_line(const std::string& line)
{
    // 找到第一个空格（分隔 method 和 path）
    size_t pos1 = line.find(' ');
    if (pos1 == std::string::npos) return false;

    // 找到第二个空格（分隔 path 和 version）
    size_t pos2 = line.find(' ', pos1 + 1);
    if (pos2 == std::string::npos) return false;

    method = line.substr(0, pos1);
    path = line.substr(pos1 + 1, pos2 - pos1 - 1);

    // 解析 HTTP 版本号 (跳过 "HTTP/")
    size_t http_pos = line.find("HTTP/", pos2);
    if (http_pos == std::string::npos) return false;
    version = line.substr(http_pos + 5);

    state = PARSE_STATE::HEADERS;
    return true;
}

void HttpRequest::parse_header(const std::string& line)
{
    // 如果是空行，说明头部解析完毕，接下来是 Body
    if (line.empty() || line == "\r") {
        // 根据是否有 Content-Length 或请求方法决定是否需要解析 Body
        if (headers.count("Content-Length") && std::stoi(headers["Content-Length"]) > 0) {
            state = PARSE_STATE::BODY;
        } else {
            // GET 或 没有 Body 的请求，直接完成
            state = PARSE_STATE::FINISH;
        }
        return;
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
    } else {
        // 如果没有冒号，说明可能是不规范的末尾行，直接进入 BODY 状态
        state = PARSE_STATE::BODY;
    }
}

void HttpRequest::parse_body(const std::string& line) 
{
    // 采用追加模式，防止 Body 被 TCP 切割成了多段
    body += line;

    size_t content_len = 0;
    if (headers.count("Content-Length")) {
        try {
            content_len = std::stoi(headers["Content-Length"]);
        } catch (...) {}
    }
    
    // 只有收集齐了 Content-Length 指定的数据大小，才算真正的完成
    if (body.size() >= content_len) {
        parse_post(); 
        state = PARSE_STATE::FINISH;
    }
}

void HttpRequest::parse_path() 
{
    // 如果用户直接访问根目录，默认重定向到主页
    if (path == "/") {
        path = "/index.html"; 
    } else {
        // 过滤目录穿越攻击 (过滤所有的 "../")
        size_t pos;
        while ((pos = path.find("../")) != std::string::npos) {
            path.replace(pos, 3, "");
        }
    }
}

void HttpRequest::parse_post() 
{
    // 目前仅支持 application/x-www-form-urlencoded 格式的 POST 表单解析
    if (method == "POST" && headers["Content-Type"] == "application/x-www-form-urlencoded") {
        if(body.empty()) return;
        
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

bool HttpRequest::is_keep_alive() const 
{
    if (headers.count("Connection")) {
        // HTTP/1.1 默认是长连接，除非显式指定 close；或者明确指定了 keep-alive
        return headers.at("Connection") == "keep-alive" && version == "1.1";
    }
    return false;
}

std::string HttpRequest::get_post(const std::string& key) const 
{
    if (post.count(key)) {
        return post.at(key);
    }
    
    return "";
}
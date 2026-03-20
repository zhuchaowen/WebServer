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
        // 在缓冲区中查找下一行的结尾 "\r\n"
        const char* line_end = std::search(buff.peek(), buff.begin_write_const(), CRLF, CRLF + 2);
        
        // 提取这一行的字符串内容用于解析
        std::string line(buff.peek(), line_end);

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
                if (buff.readable_bytes() <= 2) {
                    // 如果缓冲区只剩下 "\r\n"，说明没有 Body，解析可以直接结束 (例如普通的 GET 请求)
                    state = PARSE_STATE::FINISH;
                }
                break;
            case PARSE_STATE::BODY:
                parse_body(line);
                break;  
            default:
                break;
        }

        // 如果已经读到了缓冲区末尾，跳出循环等待下一次数据到来
        if (line_end == buff.begin_write_const()) { 
            break; 
        }

        // 解析完一行后，将 Buffer 的读游标向后移动，跳过当前行和 \r\n
        buff.retrieve_until(line_end + 2);
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
        state = PARSE_STATE::BODY;
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
    body = line;
    // 如果是 POST 请求，尝试解析表单数据
    parse_post(); 
    state = PARSE_STATE::FINISH;
}

void HttpRequest::parse_path() 
{
    // 如果用户直接访问根目录，默认重定向到主页
    if (path == "/") {
        path = "/index.html"; 
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
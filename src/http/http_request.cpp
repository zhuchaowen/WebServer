#include "http_request.h"
#include <regex>

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
    // 使用正则表达式匹配请求行，例如：GET /index.html HTTP/1.1
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;
    
    if (std::regex_match(line, subMatch, patten)) {
        method = subMatch[1];
        path = subMatch[2];
        version = subMatch[3];

        // 状态转移：接下来准备解析请求头
        state = PARSE_STATE::HEADERS; 

        return true;
    }
    
    return false;
}

void HttpRequest::parse_header(const std::string& line) 
{
    // 遇到空行，说明头部字段解析完毕
    if (line.empty()) {
        if (method == "POST") {
            // POST 请求需要继续解析 Body
            state = PARSE_STATE::BODY; 
        } else {
            // GET 请求直接结束
            state = PARSE_STATE::FINISH; 
        }
        
        return;
    }
    
    // 使用正则表达式提取 Header 的 Key 和 Value，例如：Connection: keep-alive
    std::regex patten("^([^:]*): ?(.*)$");
    std::smatch subMatch;
    if (std::regex_match(line, subMatch, patten)) {
        headers[subMatch[1]] = subMatch[2];
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
#include "http_response.h"
#include <unordered_map>
#include <sys/mman.h>
#include <fcntl.h>
#include "log.h"

// 全局静态字典
// MIME 类型
const std::unordered_map<std::string, std::string> SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css" },
    { ".js",    "text/javascript" },
};

// 状态码描述
const std::unordered_map<int, std::string> CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

HttpResponse::HttpResponse() 
{
    code = -1;
    is_keep_alive = false;
    path = root_dir = "";

    mm_file = nullptr; 
    mm_file_stat = {};
}

HttpResponse::~HttpResponse() 
{
    // 析构时自动释放内存映射，防止泄漏
    unmap_file(); 
}

void HttpResponse::init(const std::string& _root_dir, const std::string& _path, bool _is_keep_alive, int _code)
{
    // 如果复用该对象时之前已经映射了文件，必须先释放
    if (mm_file) { 
        unmap_file(); 
    }
    
    code = _code;
    is_keep_alive = _is_keep_alive;
    path = _path;
    root_dir = _root_dir;

    mm_file = nullptr; 
    mm_file_stat = { 0 };
}

void HttpResponse::make_response(Buffer& buff) 
{
    // 拼接绝对路径
    std::string full_path = root_dir + path;
    
    if (stat(full_path.data(), &mm_file_stat) < 0 || S_ISDIR(mm_file_stat.st_mode)) {
        // 找不到文件或请求的是目录
        code = 404;
        LOG_WARN("File Not Found: %s", (root_dir + path).c_str());
    } else if (!(mm_file_stat.st_mode & S_IROTH)) {
        // 没有读取权限
        code = 403;
        LOG_WARN("File Forbidden: %s", (root_dir + path).c_str());
    } else if (code == -1) { 
        code = 200;
    }
    
    // 如果发生错误，尝试将 path 替换为错误页面路径
    error_html(); 
    
    // 依次将状态行、响应头追加到 Buffer 中
    add_state_line(buff);
    add_header(buff);
    add_content(buff);
}

void HttpResponse::unmap_file() noexcept 
{
    if (mm_file) {
        // 调用 munmap 释放内存映射
        munmap(mm_file, mm_file_stat.st_size);
        mm_file = nullptr;
    }
}

void HttpResponse::error_html() 
{
    if (CODE_STATUS.count(code) == 1 && code != 200) {
        // 尝试重定向到对应的错误页面，如 /404.html, /403.html
        path = "/error/" + std::to_string(code) + ".html"; 
        std::string full_path = root_dir + path;
        
        // 检查该错误页面文件是否存在
        if (stat(full_path.data(), &mm_file_stat) < 0) {
            // 如果连错误页面都找不到，会在 add_content 中通过 error_content 兜底
            mm_file_stat.st_size = 0; 
        }
    }
}

void HttpResponse::add_state_line(Buffer& buff) 
{
    std::string status;
    if (CODE_STATUS.count(code) == 1) {
        status = CODE_STATUS.at(code);
    } else {
        code = 400;
        status = CODE_STATUS.at(400);
    }

    // 格式: HTTP/1.1 200 OK\r\n
    buff.append("HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n");
}

void HttpResponse::add_header(Buffer& buff) const
{
    buff.append("Connection: ");
    if (is_keep_alive) {
        buff.append("keep-alive\r\n");
        // 告诉客户端长连接的保持时间和最大请求数
        buff.append("Keep-Alive: max=6, timeout=120\r\n"); 
    } else{
        buff.append("close\r\n");
    }
    
    // 动态获取 MIME 类型
    buff.append("Content-type: " + get_file_type() + "\r\n");
}

void HttpResponse::add_content(Buffer& buff) 
{
    std::string full_path = root_dir + path;
    int src_fd = open(full_path.data(), O_RDONLY);
    
    if (src_fd < 0) { 
        // 文件打开失败，返回一段动态生成的 HTML 错误文本
        error_content(buff, "File NotFound!"); 
        return; 
    }
    
    // 将文件映射到内存，提高文件发送效率 (配合 writev 零拷贝)
    // PROT_READ 表示映射区域可读；MAP_PRIVATE 表示建立一个写入时拷贝的私有映射
    mm_file = static_cast<char *>(mmap(nullptr, mm_file_stat.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0));
    // 映射完成后，底层文件描述符可以立即关闭，不影响内存映射
    close(src_fd); 
    
    // 响应头最后追加文件长度，并以一个空行 \r\n 标志响应头的结束
    buff.append("Content-length: " + std::to_string(mm_file_stat.st_size) + "\r\n\r\n");
}

std::string HttpResponse::get_file_type() const
{
    std::string::size_type idx = path.find_last_of('.');
    if (idx == std::string::npos) { 
        return "text/plain"; 
    }

    std::string suffix = path.substr(idx);
    if (SUFFIX_TYPE.count(suffix) == 1) { 
        return SUFFIX_TYPE.at(suffix); 
    }

    // 未知类型默认按照纯文本处理
    return "text/plain";
}

void HttpResponse::error_content(Buffer& buff, const std::string& message) const
{
    // 动态生成一段简易的 HTML 作为 Body
    std::string body = "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    body += std::to_string(code) + " : " + CODE_STATUS.at(code) + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";
    
    buff.append("Content-length: " + std::to_string(body.size()) + "\r\n\r\n");
    buff.append(body);
}
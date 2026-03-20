#ifndef WEBSERVER_HTTP_RESPONSE_H
#define WEBSERVER_HTTP_RESPONSE_H

#include <string>
#include <sys/stat.h>
#include "buffer.h"

class HttpResponse {
private:
    int code;                   // HTTP 状态码
    bool is_keep_alive;         // 是否保持长连接
    std::string path;           // 请求的资源路径 (相对于根目录)
    std::string root_dir;       // 网站静态资源的根目录绝对路径

    char* mm_file;              // 指向 mmap 映射到内存中的文件内容指针
    struct stat mm_file_stat{};   // 存储目标文件的状态信息 (如文件大小、是否是目录)

    // 组装报文的具体内部逻辑
    void add_state_line(Buffer& buff);
    void add_header(Buffer& buff) const;
    void add_content(Buffer& buff);

    // 根据文件后缀获取对应的 MIME Type
    std::string get_file_type() const;

    // 发生错误时，将请求路径重定向到对应的错误 HTML 页面 (如 404.html)
    void error_html();

    // 当连错误页面文件都找不到时，回退生成一段纯文本的 HTML 错误提示
    void error_content(Buffer& buff, const std::string& message) const;
public:
    HttpResponse();
    ~HttpResponse();

    // 初始化响应对象状态
    void init(const std::string& _root_dir, const std::string& _path, bool _is_keep_alive = false, int _code = -1);

    // 生成 HTTP 响应报文（状态行 + 响应头），并追加到写缓冲区
    void make_response(Buffer& buff);

    // 安全释放 mmap 映射的内存
    void unmap_file() noexcept;

    // 状态查询接口
    char* get_file() const noexcept { return mm_file; }
    size_t get_file_len() const noexcept { return mm_file_stat.st_size; }
    int get_code() const noexcept { return code; }
};

#endif //WEBSERVER_HTTP_RESPONSE_H
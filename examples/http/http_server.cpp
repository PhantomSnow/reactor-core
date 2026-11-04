// 極簡 HTTP/1.1 server：支援 keep-alive 與 pipelining，GET / 回傳 "Hello, World!"。
// 用來展示 Buffer 如何處理「一次收到半個或多個請求」，並配合 wrk 壓測。
// 用法：http_server [port=8080] [io_threads=0]
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "tinynet/EventLoop.h"
#include "tinynet/TcpServer.h"

using namespace tinynet;

namespace {

constexpr std::string_view kHeaderEnd = "\r\n\r\n";
constexpr std::size_t kMaxHeaderBytes = 64 * 1024;

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

struct Request {
    std::string_view method;
    std::string_view path;
    bool keep_alive = true;
    std::size_t content_length = 0;
};

// 解析一個完整的 header 區塊（不含結尾的 \r\n\r\n）。格式錯誤回傳 false。
bool parse_request(std::string_view head, Request* req) {
    const std::size_t line_end = head.find("\r\n");
    std::string_view request_line = head.substr(0, line_end);
    const std::size_t sp1 = request_line.find(' ');
    const std::size_t sp2 = request_line.rfind(' ');
    if (sp1 == std::string_view::npos || sp2 == sp1) return false;
    req->method = request_line.substr(0, sp1);
    req->path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = request_line.substr(sp2 + 1);
    req->keep_alive = version == "HTTP/1.1";  // HTTP/1.0 預設不 keep-alive

    std::string_view rest = line_end == std::string_view::npos ? "" : head.substr(line_end + 2);
    while (!rest.empty()) {
        const std::size_t eol = rest.find("\r\n");
        std::string_view line = rest.substr(0, eol);
        rest = eol == std::string_view::npos ? "" : rest.substr(eol + 2);
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = trim(line.substr(0, colon));
        const std::string_view value = trim(line.substr(colon + 1));
        if (iequals(key, "Connection")) {
            if (iequals(value, "close")) req->keep_alive = false;
            if (iequals(value, "keep-alive")) req->keep_alive = true;
        } else if (iequals(key, "Content-Length")) {
            req->content_length = std::strtoul(std::string(value).c_str(), nullptr, 10);
        }
    }
    return true;
}

void append_response(std::string* out, int status, std::string_view reason, std::string_view body,
                     bool keep_alive) {
    out->append("HTTP/1.1 ").append(std::to_string(status)).append(" ").append(reason);
    out->append("\r\nContent-Type: text/plain\r\nContent-Length: ").append(std::to_string(body.size()));
    out->append(keep_alive ? "\r\nConnection: keep-alive\r\n\r\n" : "\r\nConnection: close\r\n\r\n");
    out->append(body);
}

void on_message(const TcpConnectionPtr& conn, Buffer* buf) {
    std::string response;
    bool close_after = false;

    // 一次 read 可能包含多個請求（pipelining），也可能只有半個：迴圈處理完整的，剩下的留在 buffer
    while (true) {
        const std::string_view data = buf->view();
        const std::size_t header_end = data.find(kHeaderEnd);
        if (header_end == std::string_view::npos) {
            if (data.size() > kMaxHeaderBytes) {
                append_response(&response, 431, "Request Header Fields Too Large", "", false);
                close_after = true;
            }
            break;
        }
        Request req;
        if (!parse_request(data.substr(0, header_end), &req)) {
            append_response(&response, 400, "Bad Request", "", false);
            close_after = true;
            break;
        }
        const std::size_t total = header_end + kHeaderEnd.size() + req.content_length;
        if (data.size() < total) {
            break;  // body 還沒收齊，等下一次 read
        }

        if (req.method == "GET" && req.path == "/") {
            append_response(&response, 200, "OK", "Hello, World!", req.keep_alive);
        } else {
            append_response(&response, 404, "Not Found", "Not Found", req.keep_alive);
        }
        buf->retrieve(total);
        if (!req.keep_alive) {
            close_after = true;
            break;
        }
    }

    if (!response.empty()) {
        conn->send(response);
    }
    if (close_after) {
        conn->shutdown();  // 等 output buffer 送完才半關閉
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const auto port = static_cast<uint16_t>(argc > 1 ? std::atoi(argv[1]) : 8080);
    const int threads = argc > 2 ? std::atoi(argv[2]) : 0;

    EventLoop loop;
    TcpServer server(&loop, InetAddress(port), "http");
    server.set_thread_num(threads);
    server.set_connection_callback([](const TcpConnectionPtr& conn) {
        if (conn->connected()) conn->set_tcp_nodelay(true);
    });
    server.set_message_callback(on_message);
    server.start();

    std::printf("http server listening on %s with %d I/O thread(s)\n",
                server.listen_address().to_ip_port().c_str(), threads);
    loop.loop();
}

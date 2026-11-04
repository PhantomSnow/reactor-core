// Echo server：收到什麼就回什麼。
// 用法：echo_server [port=8888] [io_threads=0]
#include <cstdio>
#include <cstdlib>

#include "tinynet/EventLoop.h"
#include "tinynet/TcpServer.h"

using namespace tinynet;

int main(int argc, char* argv[]) {
    const auto port = static_cast<uint16_t>(argc > 1 ? std::atoi(argv[1]) : 8888);
    const int threads = argc > 2 ? std::atoi(argv[2]) : 0;

    EventLoop loop;
    TcpServer server(&loop, InetAddress(port), "echo");
    server.set_thread_num(threads);

    server.set_connection_callback([](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            conn->set_tcp_nodelay(true);
        }
    });
    server.set_message_callback([](const TcpConnectionPtr& conn, Buffer* buf) {
        conn->send(buf);  // 在 I/O 執行緒內，直接從 input buffer 送出，零額外複製
    });

    server.start();
    std::printf("echo server listening on %s with %d I/O thread(s)\n",
                server.listen_address().to_ip_port().c_str(), threads);
    loop.loop();
}

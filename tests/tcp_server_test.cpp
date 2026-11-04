// 端對端整合測試：起一個多執行緒 echo server，用真正的 TCP client 打它。
#include "tinynet/TcpServer.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "test_util.h"
#include "tinynet/EventLoop.h"

using namespace tinynet;
using namespace std::chrono_literals;

namespace {

int connect_blocking(const InetAddress& addr) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (::connect(fd, addr.sockaddr_ptr(), InetAddress::kSockaddrLen) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool write_all(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n <= 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

std::string read_exact(int fd, std::size_t len) {
    std::string out;
    out.resize(len);
    std::size_t off = 0;
    while (off < len) {
        ssize_t n = ::read(fd, &out[off], len - off);
        if (n <= 0) break;
        off += static_cast<std::size_t>(n);
    }
    out.resize(off);
    return out;
}

struct EchoFixture {
    EventLoop loop;
    TcpServer server;
    std::atomic<int> up{0};
    std::atomic<int> down{0};
    std::mutex mutex;
    std::set<std::thread::id> io_threads;

    explicit EchoFixture(int threads) : server(&loop, InetAddress(0, true), "echo-test") {
        server.set_thread_num(threads);
        server.set_connection_callback([this](const TcpConnectionPtr& conn) {
            if (conn->connected()) {
                ++up;
                std::lock_guard<std::mutex> lock(mutex);
                io_threads.insert(std::this_thread::get_id());
            } else {
                ++down;
            }
        });
        server.set_message_callback([](const TcpConnectionPtr& conn, Buffer* buf) { conn->send(buf); });
        server.start();
    }
};

}  // namespace

TEST(multithreaded_echo_many_clients) {
    constexpr int kThreads = 4;
    constexpr int kClients = 40;
    constexpr int kRounds = 20;
    EchoFixture fx(kThreads);
    const InetAddress addr = fx.server.listen_address();
    std::atomic<int> ok_clients{0};

    std::thread driver([&] {
        std::vector<std::thread> clients;
        for (int c = 0; c < kClients; ++c) {
            clients.emplace_back([&, c] {
                int fd = connect_blocking(addr);
                if (fd < 0) return;
                bool ok = true;
                for (int r = 0; r < kRounds && ok; ++r) {
                    const std::string msg = "client-" + std::to_string(c) + "-round-" + std::to_string(r) + "\n";
                    ok = write_all(fd, msg) && read_exact(fd, msg.size()) == msg;
                }
                ::close(fd);
                if (ok) ++ok_clients;
            });
        }
        for (auto& t : clients) t.join();
        // 等 server 端處理完所有斷線
        for (int i = 0; i < 200 && fx.down.load() < kClients; ++i) std::this_thread::sleep_for(5ms);
        fx.loop.quit();
    });
    fx.loop.loop();
    driver.join();

    CHECK_EQ(ok_clients.load(), kClients);
    CHECK_EQ(fx.up.load(), kClients);
    CHECK_EQ(fx.down.load(), kClients);
    // round-robin：連線應該分散到全部 4 條 I/O 執行緒，而且都不是 base loop 執行緒
    CHECK_EQ(fx.io_threads.size(), static_cast<std::size_t>(kThreads));
    CHECK(fx.io_threads.count(std::this_thread::get_id()) == 0);
}

TEST(large_message_exercises_output_buffer) {
    // 4MB 遠大於 socket 發送緩衝區：server 一定會遇到 partial write，
    // 剩下的資料進 output buffer，靠 EPOLLOUT + handle_write 送完
    EchoFixture fx(2);
    const InetAddress addr = fx.server.listen_address();
    const std::string payload = [] {
        std::string s(4 * 1024 * 1024, '\0');
        for (std::size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>('a' + i % 26);
        return s;
    }();
    std::string echoed;

    std::thread driver([&] {
        int fd = connect_blocking(addr);
        if (fd >= 0) {
            // 邊寫邊讀，避免雙方的緩衝區都滿而互相卡死
            std::thread writer([&] { write_all(fd, payload); });
            echoed = read_exact(fd, payload.size());
            writer.join();
            ::close(fd);
        }
        for (int i = 0; i < 200 && fx.down.load() < 1; ++i) std::this_thread::sleep_for(5ms);
        fx.loop.quit();
    });
    fx.loop.loop();
    driver.join();

    CHECK_EQ(echoed.size(), payload.size());
    CHECK(echoed == payload);
}

TEST(server_shutdown_half_closes_after_flush) {
    EventLoop loop;
    TcpServer server(&loop, InetAddress(0, true), "bye");
    server.set_thread_num(1);
    server.set_connection_callback([](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            conn->send("goodbye\n");
            conn->shutdown();  // 送完才 FIN
        }
    });
    server.start();
    std::string received;

    std::thread driver([&] {
        int fd = connect_blocking(server.listen_address());
        char buf[64];
        ssize_t n;
        while ((n = ::read(fd, buf, sizeof(buf))) > 0) received.append(buf, static_cast<std::size_t>(n));
        ::close(fd);  // read 回傳 0 → server 已半關閉
        loop.run_after(20ms, [&] { loop.quit(); });
    });
    loop.loop();
    driver.join();
    CHECK_EQ(received, std::string("goodbye\n"));
}

TEST(send_from_non_io_thread) {
    EventLoop loop;
    TcpServer server(&loop, InetAddress(0, true), "push");
    server.set_thread_num(2);
    std::mutex mutex;
    TcpConnectionPtr saved;
    server.set_connection_callback([&](const TcpConnectionPtr& conn) {
        if (conn->connected()) {
            std::lock_guard<std::mutex> lock(mutex);
            saved = conn;
        }
    });
    server.start();
    std::string received;

    std::thread driver([&] {
        int fd = connect_blocking(server.listen_address());
        TcpConnectionPtr conn;
        for (int i = 0; i < 200 && !conn; ++i) {
            std::this_thread::sleep_for(2ms);
            std::lock_guard<std::mutex> lock(mutex);
            conn = saved;
        }
        if (conn) {
            // 這條執行緒既不是 base loop 也不是 I/O loop：send 必須轉交給連線所屬的 loop
            for (int i = 0; i < 100; ++i) conn->send("x");
        }
        received = read_exact(fd, 100);
        ::close(fd);
        {
            std::lock_guard<std::mutex> lock(mutex);
            saved.reset();
        }
        conn.reset();
        loop.run_after(20ms, [&] { loop.quit(); });
    });
    loop.loop();
    driver.join();
    CHECK_EQ(received, std::string(100, 'x'));
}

TEST_MAIN()

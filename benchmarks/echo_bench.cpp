// Ping-pong 壓測 client：開 N 條連線，每條連線送出 msg_size bytes，收到完整回應後才送下一個。
// 用原始 epoll 寫，不依賴 tinynet，避免「用自己測自己」的偏差。
//
// 用法：echo_bench <ip> <port> <connections> <msg_size> <seconds>
// 輸出：QPS（每秒完成的來回次數）、吞吐量、延遲 p50 / p99。
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Conn {
    int fd = -1;
    bool connected = false;
    std::size_t write_off = 0;   // 本輪訊息已送出的位元組
    std::size_t received = 0;    // 本輪訊息已收到的位元組
    Clock::time_point sent_at;
};

void update_events(int epfd, Conn* c, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.ptr = c;
    ::epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

// 回傳 true 表示整則訊息已送完
bool try_write(int epfd, Conn* c, const std::string& msg) {
    while (c->write_off < msg.size()) {
        ssize_t n = ::send(c->fd, msg.data() + c->write_off, msg.size() - c->write_off, MSG_NOSIGNAL);
        if (n > 0) {
            c->write_off += static_cast<std::size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            update_events(epfd, c, EPOLLIN | EPOLLOUT);
            return false;
        } else {
            std::perror("send");
            std::exit(1);
        }
    }
    update_events(epfd, c, EPOLLIN);
    return true;
}

void start_round(int epfd, Conn* c, const std::string& msg) {
    c->write_off = 0;
    c->received = 0;
    c->sent_at = Clock::now();
    try_write(epfd, c, msg);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s <ip> <port> <connections> <msg_size> <seconds>\n", argv[0]);
        return 1;
    }
    const char* ip = argv[1];
    const int port = std::atoi(argv[2]);
    const int num_conns = std::atoi(argv[3]);
    const std::size_t msg_size = std::strtoul(argv[4], nullptr, 10);
    const int seconds = std::atoi(argv[5]);
    const auto warmup = std::chrono::seconds(1);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    ::inet_pton(AF_INET, ip, &addr.sin_addr);

    const std::string msg(msg_size, 'm');
    std::vector<char> scratch(std::max<std::size_t>(msg_size, 65536));
    std::vector<Conn> conns(static_cast<std::size_t>(num_conns));

    int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    for (Conn& c : conns) {
        c.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        int one = 1;
        ::setsockopt(c.fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (::connect(c.fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 && errno != EINPROGRESS) {
            std::perror("connect");
            return 1;
        }
        epoll_event ev{};
        ev.events = EPOLLOUT;  // 非阻塞 connect 完成時會變成可寫
        ev.data.ptr = &c;
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, c.fd, &ev);
    }

    std::vector<uint32_t> latencies_us;
    latencies_us.reserve(4'000'000);
    uint64_t completed = 0;

    const auto begin = Clock::now();
    const auto measure_start = begin + warmup;
    const auto deadline = measure_start + std::chrono::seconds(seconds);
    std::vector<epoll_event> events(static_cast<std::size_t>(std::max(num_conns, 1)));

    while (true) {
        const auto now = Clock::now();
        if (now >= deadline) break;
        const int n = ::epoll_wait(epfd, events.data(), static_cast<int>(events.size()), 100);
        for (int i = 0; i < n; ++i) {
            Conn* c = static_cast<Conn*>(events[i].data.ptr);
            if (!c->connected) {
                int err = 0;
                socklen_t len = sizeof(err);
                ::getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err != 0) {
                    std::fprintf(stderr, "connect failed: %s\n", std::strerror(err));
                    return 1;
                }
                c->connected = true;
                start_round(epfd, c, msg);
                continue;
            }
            if (events[i].events & EPOLLOUT) {
                try_write(epfd, c, msg);
            }
            if (events[i].events & (EPOLLIN | EPOLLHUP | EPOLLERR)) {
                while (true) {
                    ssize_t r = ::read(c->fd, scratch.data(), scratch.size());
                    if (r > 0) {
                        c->received += static_cast<std::size_t>(r);
                    } else if (r == 0) {
                        std::fprintf(stderr, "server closed connection\n");
                        return 1;
                    } else {
                        break;  // EAGAIN
                    }
                }
                if (c->received >= msg_size) {
                    const auto done = Clock::now();
                    if (done >= measure_start && done < deadline) {
                        ++completed;
                        latencies_us.push_back(static_cast<uint32_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(done - c->sent_at).count()));
                    }
                    start_round(epfd, c, msg);
                }
            }
        }
    }

    for (Conn& c : conns) ::close(c.fd);
    ::close(epfd);

    std::sort(latencies_us.begin(), latencies_us.end());
    auto pct = [&](double p) -> uint32_t {
        if (latencies_us.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(p * static_cast<double>(latencies_us.size() - 1));
        return latencies_us[idx];
    };
    const double qps = static_cast<double>(completed) / seconds;
    const double mib_per_s = qps * static_cast<double>(msg_size) * 2 / (1024.0 * 1024.0);  // 雙向
    std::printf("connections=%d msg_size=%zu duration=%ds\n", num_conns, msg_size, seconds);
    std::printf("qps=%.0f throughput=%.1fMiB/s p50=%uus p99=%uus\n", qps, mib_per_s, pct(0.50), pct(0.99));
    return 0;
}

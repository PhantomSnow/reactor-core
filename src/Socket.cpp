#include "tinynet/Socket.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <system_error>
#include <utility>

#include "tinynet/InetAddress.h"

namespace tinynet {

namespace {

[[noreturn]] void throw_errno(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

void set_bool_option(int fd, int level, int name, bool on, const char* what) {
    int value = on ? 1 : 0;
    if (::setsockopt(fd, level, name, &value, sizeof(value)) < 0) {
        throw_errno(what);
    }
}

}  // namespace

Socket::~Socket() {
    if (sockfd_ >= 0) {
        ::close(sockfd_);
    }
}

Socket::Socket(Socket&& rhs) noexcept : sockfd_(std::exchange(rhs.sockfd_, -1)) {}

Socket& Socket::operator=(Socket&& rhs) noexcept {
    if (this != &rhs) {
        // 先關掉自己原本擁有的 fd，再接管對方的；否則舊 fd 會洩漏
        if (sockfd_ >= 0) {
            ::close(sockfd_);
        }
        sockfd_ = std::exchange(rhs.sockfd_, -1);
    }
    return *this;
}

Socket Socket::create_nonblocking_tcp() {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (fd < 0) {
        throw_errno("socket");
    }
    return Socket(fd);
}

int Socket::release() noexcept { return std::exchange(sockfd_, -1); }

void Socket::bind(const InetAddress& addr) {
    if (::bind(sockfd_, addr.sockaddr_ptr(), InetAddress::kSockaddrLen) < 0) {
        throw_errno("bind");
    }
}

void Socket::listen() {
    if (::listen(sockfd_, SOMAXCONN) < 0) {
        throw_errno("listen");
    }
}

int Socket::accept(InetAddress* peer) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    // accept4 一次把新 fd 設成 non-blocking + close-on-exec
    int connfd = ::accept4(sockfd_, reinterpret_cast<sockaddr*>(&addr), &len,
                           SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (connfd >= 0 && peer != nullptr) {
        *peer = InetAddress(addr);
    }
    return connfd;
}

void Socket::shutdown_write() { ::shutdown(sockfd_, SHUT_WR); }

void Socket::set_nonblocking(bool on) {
    int flags = ::fcntl(sockfd_, F_GETFL, 0);
    if (flags < 0) {
        throw_errno("fcntl(F_GETFL)");
    }
    flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(sockfd_, F_SETFL, flags) < 0) {
        throw_errno("fcntl(F_SETFL)");
    }
}

void Socket::set_reuse_addr(bool on) {
    // 讓 server 重啟時能立刻 bind 到仍有 TIME_WAIT 連線的 port
    set_bool_option(sockfd_, SOL_SOCKET, SO_REUSEADDR, on, "SO_REUSEADDR");
}

void Socket::set_reuse_port(bool on) {
    set_bool_option(sockfd_, SOL_SOCKET, SO_REUSEPORT, on, "SO_REUSEPORT");
}

void Socket::set_tcp_nodelay(bool on) {
    // 關掉 Nagle 演算法，小封包不必等 ACK 就送出，降低延遲
    set_bool_option(sockfd_, IPPROTO_TCP, TCP_NODELAY, on, "TCP_NODELAY");
}

void Socket::set_keep_alive(bool on) {
    set_bool_option(sockfd_, SOL_SOCKET, SO_KEEPALIVE, on, "SO_KEEPALIVE");
}

int Socket::get_socket_error(int fd) noexcept {
    int err = 0;
    socklen_t len = sizeof(err);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        return errno;
    }
    return err;
}

}  // namespace tinynet

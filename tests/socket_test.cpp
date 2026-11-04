#include "tinynet/Socket.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>
#include <string>
#include <utility>

#include "test_util.h"
#include "tinynet/InetAddress.h"

using tinynet::InetAddress;
using tinynet::Socket;

namespace {
bool fd_is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1 || errno != EBADF; }
}  // namespace

TEST(destructor_closes_fd) {
    int fd = -1;
    {
        Socket s = Socket::create_nonblocking_tcp();
        fd = s.fd();
        CHECK(s.is_valid());
        CHECK(fd_is_open(fd));
    }
    CHECK(!fd_is_open(fd));
}

TEST(created_with_nonblock_and_cloexec) {
    Socket s = Socket::create_nonblocking_tcp();
    CHECK((::fcntl(s.fd(), F_GETFL) & O_NONBLOCK) != 0);
    CHECK((::fcntl(s.fd(), F_GETFD) & FD_CLOEXEC) != 0);
    s.set_nonblocking(false);
    CHECK((::fcntl(s.fd(), F_GETFL) & O_NONBLOCK) == 0);
}

TEST(move_construct_transfers_ownership) {
    Socket a = Socket::create_nonblocking_tcp();
    const int fd = a.fd();
    Socket b(std::move(a));
    CHECK(!a.is_valid());  // NOLINT(bugprone-use-after-move)
    CHECK_EQ(b.fd(), fd);
    CHECK(fd_is_open(fd));
}

TEST(move_assign_closes_old_fd) {
    Socket a = Socket::create_nonblocking_tcp();
    Socket b = Socket::create_nonblocking_tcp();
    const int fd_a = a.fd();
    const int fd_b = b.fd();
    b = std::move(a);
    CHECK_EQ(b.fd(), fd_a);
    CHECK(!fd_is_open(fd_b));  // b 原本的 fd 必須被關掉，否則洩漏
    CHECK(fd_is_open(fd_a));
}

TEST(self_move_assign_is_safe) {
    Socket a = Socket::create_nonblocking_tcp();
    const int fd = a.fd();
    Socket& ref = a;
    a = std::move(ref);
    CHECK_EQ(a.fd(), fd);
    CHECK(fd_is_open(fd));
}

TEST(release_gives_up_ownership) {
    int fd = -1;
    {
        Socket s = Socket::create_nonblocking_tcp();
        fd = s.release();
        CHECK(!s.is_valid());
    }
    CHECK(fd_is_open(fd));
    ::close(fd);
}

TEST(bind_listen_accept) {
    Socket listener = Socket::create_nonblocking_tcp();
    listener.set_reuse_addr(true);
    listener.bind(InetAddress(0, /*loopback_only=*/true));
    listener.listen();
    const InetAddress local = InetAddress::local_of(listener.fd());
    CHECK(local.port() != 0);
    CHECK_EQ(local.ip(), std::string("127.0.0.1"));

    // 沒有連線時，非阻塞 accept 應立即回傳 -1 / EAGAIN，而不是卡住
    InetAddress peer;
    CHECK_EQ(listener.accept(&peer), -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK);

    Socket client = Socket::create_nonblocking_tcp();
    client.set_nonblocking(false);
    CHECK(::connect(client.fd(), local.sockaddr_ptr(), InetAddress::kSockaddrLen) == 0);

    int connfd = -1;
    for (int i = 0; i < 100 && connfd < 0; ++i) {
        connfd = listener.accept(&peer);
        if (connfd < 0) ::usleep(1000);
    }
    CHECK(connfd >= 0);
    Socket conn(connfd);
    CHECK((::fcntl(conn.fd(), F_GETFL) & O_NONBLOCK) != 0);
    CHECK_EQ(peer.ip(), std::string("127.0.0.1"));
    CHECK_EQ(peer.port(), InetAddress::local_of(client.fd()).port());
}

TEST(inet_address_formatting) {
    InetAddress addr("10.1.2.3", 8080);
    CHECK_EQ(addr.to_ip_port(), std::string("10.1.2.3:8080"));
    bool threw = false;
    try {
        InetAddress bad("not-an-ip", 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_MAIN()

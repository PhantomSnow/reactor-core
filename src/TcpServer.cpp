#include "tinynet/TcpServer.h"

#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "tinynet/EventLoop.h"

namespace tinynet {

namespace {

int open_idle_fd() { return ::open("/dev/null", O_RDONLY | O_CLOEXEC); }

}  // namespace

TcpServer::TcpServer(EventLoop* loop, const InetAddress& listen_addr, std::string name,
                     Option option)
    : loop_(loop),
      name_(std::move(name)),
      accept_socket_(Socket::create_nonblocking_tcp()),
      listen_addr_(listen_addr),
      accept_channel_(loop, accept_socket_.fd()),
      idle_fd_(open_idle_fd()),
      thread_pool_(std::make_unique<EventLoopThreadPool>(loop, name_)) {
    accept_socket_.set_reuse_addr(true);
    accept_socket_.set_reuse_port(option == Option::kReusePort);
    accept_socket_.bind(listen_addr);
    listen_addr_ = InetAddress::local_of(accept_socket_.fd());
    accept_channel_.set_read_callback([this] { handle_accept(); });
}

TcpServer::~TcpServer() {
    loop_->assert_in_loop_thread();
    accept_channel_.disable_all();
    accept_channel_.remove();
    for (auto& [name, conn] : connections_) {
        TcpConnectionPtr guard(conn);
        conn.reset();
        guard->loop()->run_in_loop([guard] { guard->connect_destroyed(); });
    }
    if (idle_fd_ >= 0) {
        ::close(idle_fd_);
    }
    // thread_pool_ 析構時會讓各 I/O loop 執行完上面排入的 connect_destroyed 後結束
}

void TcpServer::set_thread_num(int num_threads) {
    assert(num_threads >= 0);
    thread_pool_->set_thread_num(num_threads);
}

void TcpServer::start() {
    bool expected = false;
    if (started_.compare_exchange_strong(expected, true)) {
        loop_->run_in_loop([this] {
            thread_pool_->start(thread_init_cb_);
            accept_socket_.listen();
            accept_channel_.enable_reading();
        });
    }
}

void TcpServer::handle_accept() {
    loop_->assert_in_loop_thread();
    // 一次把 backlog 裡的連線都取完，短連線高併發時能少很多次 epoll_wait
    for (;;) {
        InetAddress peer_addr;
        const int connfd = accept_socket_.accept(&peer_addr);
        if (connfd >= 0) {
            new_connection(connfd, peer_addr);
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR || errno == ECONNABORTED) {
            continue;
        }
        if (errno == EMFILE || errno == ENFILE) {
            // fd 用光了：連線一直留在 backlog，LT 模式下監聽 socket 會不停可讀 → busy loop。
            // 解法：先釋放預留的 idle fd，把這條連線 accept 進來立刻關掉，再把 idle fd 拿回來。
            ::close(idle_fd_);
            idle_fd_ = ::accept(accept_socket_.fd(), nullptr, nullptr);
            if (idle_fd_ >= 0) {
                ::close(idle_fd_);
            }
            idle_fd_ = open_idle_fd();
            return;
        }
        std::fprintf(stderr, "TcpServer::handle_accept: %s\n", std::strerror(errno));
        return;
    }
}

void TcpServer::new_connection(int sockfd, const InetAddress& peer_addr) {
    loop_->assert_in_loop_thread();
    EventLoop* io_loop = thread_pool_->get_next_loop();
    std::string conn_name = name_ + "-" + listen_addr_.to_ip_port() + "#" + std::to_string(next_conn_id_++);

    auto conn = std::make_shared<TcpConnection>(io_loop, std::move(conn_name), sockfd,
                                                InetAddress::local_of(sockfd), peer_addr);
    connections_[conn->name()] = conn;
    conn->set_connection_callback(connection_cb_);
    conn->set_message_callback(message_cb_);
    conn->set_write_complete_callback(write_complete_cb_);
    conn->set_close_callback([this](const TcpConnectionPtr& c) { remove_connection(c); });
    // 之後這條連線的一切都在 io_loop 執行緒處理
    io_loop->run_in_loop([conn] { conn->connect_established(); });
}

void TcpServer::remove_connection(const TcpConnectionPtr& conn) {
    // 這個回呼在 I/O 執行緒觸發，但 connections_ 只能在 base loop 存取，所以轉過去
    loop_->run_in_loop([this, conn] { remove_connection_in_loop(conn); });
}

void TcpServer::remove_connection_in_loop(const TcpConnectionPtr& conn) {
    loop_->assert_in_loop_thread();
    connections_.erase(conn->name());
    // 用 queue_in_loop 而不是 run_in_loop：此時可能還在這條連線 Channel::handle_event 的呼叫堆疊上，
    // 必須等事件處理完才能把 Channel 從 Poller 移除。lambda 持有 conn，物件會活到那時候。
    conn->loop()->queue_in_loop([conn] { conn->connect_destroyed(); });
}

}  // namespace tinynet

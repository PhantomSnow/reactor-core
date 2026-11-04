#include "tinynet/TcpConnection.h"

#include <sys/socket.h>

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "tinynet/Channel.h"
#include "tinynet/EventLoop.h"

namespace tinynet {

TcpConnection::TcpConnection(EventLoop* loop, std::string name, int sockfd,
                             const InetAddress& local_addr, const InetAddress& peer_addr)
    : loop_(loop),
      name_(std::move(name)),
      socket_(sockfd),
      channel_(std::make_unique<Channel>(loop, sockfd)),
      local_addr_(local_addr),
      peer_addr_(peer_addr) {
    // Channel 的回呼只捕捉 this：Channel::tie 保證回呼執行期間 this 仍然存活
    channel_->set_read_callback([this] { handle_read(); });
    channel_->set_write_callback([this] { handle_write(); });
    channel_->set_close_callback([this] { handle_close(); });
    channel_->set_error_callback([this] { handle_error(); });
    socket_.set_keep_alive(true);
}

TcpConnection::~TcpConnection() { assert(state_ == State::kDisconnected); }

void TcpConnection::send(std::string_view data) {
    if (state_ != State::kConnected) {
        return;
    }
    if (loop_->is_in_loop_thread()) {
        send_in_loop(data.data(), data.size());
    } else {
        // 跨執行緒：資料必須複製一份，因為呼叫端的 buffer 在工作真正執行時可能已失效
        loop_->run_in_loop([self = shared_from_this(), msg = std::string(data)] {
            self->send_in_loop(msg.data(), msg.size());
        });
    }
}

void TcpConnection::send(Buffer* buf) {
    if (state_ != State::kConnected) {
        return;
    }
    if (loop_->is_in_loop_thread()) {
        send_in_loop(buf->peek(), buf->readable_bytes());
        buf->retrieve_all();
    } else {
        loop_->run_in_loop([self = shared_from_this(), msg = buf->retrieve_all_as_string()] {
            self->send_in_loop(msg.data(), msg.size());
        });
    }
}

void TcpConnection::send_in_loop(const char* data, std::size_t len) {
    loop_->assert_in_loop_thread();
    if (state_ == State::kDisconnected) {
        return;
    }
    ssize_t nwrote = 0;
    std::size_t remaining = len;
    bool fault = false;

    // output buffer 沒有排隊中的資料時，先直接寫，多數情況一次就寫完，省一次 epoll 往返；
    // 如果已有排隊資料，就必須接在後面，否則會打亂位元組順序。
    if (!channel_->is_writing() && output_buffer_.readable_bytes() == 0) {
        // MSG_NOSIGNAL：對端已關閉時回傳 EPIPE，而不是送出 SIGPIPE 把整個 process 殺掉
        nwrote = ::send(socket_.fd(), data, len, MSG_NOSIGNAL);
        if (nwrote >= 0) {
            remaining = len - static_cast<std::size_t>(nwrote);
            if (remaining == 0 && write_complete_cb_) {
                loop_->queue_in_loop([self = shared_from_this()] { self->write_complete_cb_(self); });
            }
        } else {
            nwrote = 0;
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                if (errno == EPIPE || errno == ECONNRESET) {
                    fault = true;
                }
            }
        }
    }

    // 沒寫完（partial write）：剩下的放進 output buffer，並關注 EPOLLOUT，
    // 等 socket 發送緩衝區有空間時由 handle_write 繼續送
    if (!fault && remaining > 0) {
        const std::size_t old_len = output_buffer_.readable_bytes();
        if (old_len + remaining >= high_water_mark_ && old_len < high_water_mark_ &&
            high_water_mark_cb_) {
            loop_->queue_in_loop([self = shared_from_this(), size = old_len + remaining] {
                self->high_water_mark_cb_(self, size);
            });
        }
        output_buffer_.append(data + nwrote, remaining);
        if (!channel_->is_writing()) {
            channel_->enable_writing();
        }
    }
}

void TcpConnection::shutdown() {
    State expected = State::kConnected;
    if (state_.compare_exchange_strong(expected, State::kDisconnecting)) {
        loop_->run_in_loop([self = shared_from_this()] { self->shutdown_in_loop(); });
    }
}

void TcpConnection::shutdown_in_loop() {
    loop_->assert_in_loop_thread();
    // 還有資料沒送完就先不關；handle_write 送完時會再呼叫一次
    if (!channel_->is_writing()) {
        socket_.shutdown_write();
    }
}

void TcpConnection::force_close() {
    State s = state_;
    if (s == State::kConnected || s == State::kDisconnecting) {
        state_ = State::kDisconnecting;
        loop_->queue_in_loop([self = shared_from_this()] { self->force_close_in_loop(); });
    }
}

void TcpConnection::force_close_in_loop() {
    loop_->assert_in_loop_thread();
    State s = state_;
    if (s == State::kConnected || s == State::kDisconnecting) {
        handle_close();
    }
}

void TcpConnection::connect_established() {
    loop_->assert_in_loop_thread();
    assert(state_ == State::kConnecting);
    state_ = State::kConnected;
    channel_->tie(shared_from_this());
    channel_->enable_reading();
    if (connection_cb_) {
        connection_cb_(shared_from_this());
    }
}

void TcpConnection::connect_destroyed() {
    loop_->assert_in_loop_thread();
    if (state_ == State::kConnected) {
        // 走到這裡代表是 server 主動關閉（例如 TcpServer 析構），沒有經過 handle_close
        state_ = State::kDisconnected;
        channel_->disable_all();
        if (connection_cb_) {
            connection_cb_(shared_from_this());
        }
    }
    channel_->remove();
}

void TcpConnection::handle_read() {
    loop_->assert_in_loop_thread();
    int saved_errno = 0;
    const ssize_t n = input_buffer_.read_fd(channel_->fd(), &saved_errno);
    if (n > 0) {
        if (message_cb_) {
            message_cb_(shared_from_this(), &input_buffer_);
        }
    } else if (n == 0) {
        // read 回傳 0：對端送了 FIN（呼叫 close 或 shutdown）
        handle_close();
    } else {
        errno = saved_errno;
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK && saved_errno != EINTR) {
            handle_error();
        }
    }
}

void TcpConnection::handle_write() {
    loop_->assert_in_loop_thread();
    if (!channel_->is_writing()) {
        return;
    }
    const ssize_t n = ::send(channel_->fd(), output_buffer_.peek(), output_buffer_.readable_bytes(),
                             MSG_NOSIGNAL);
    if (n > 0) {
        output_buffer_.retrieve(static_cast<std::size_t>(n));
        if (output_buffer_.readable_bytes() == 0) {
            // 一定要關掉 EPOLLOUT：LT 模式下 socket 幾乎永遠可寫，不關會變成 busy loop
            channel_->disable_writing();
            if (write_complete_cb_) {
                loop_->queue_in_loop([self = shared_from_this()] { self->write_complete_cb_(self); });
            }
            if (state_ == State::kDisconnecting) {
                shutdown_in_loop();
            }
        }
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        std::fprintf(stderr, "TcpConnection::handle_write [%s]: %s\n", name_.c_str(),
                     std::strerror(errno));
    }
}

void TcpConnection::handle_close() {
    loop_->assert_in_loop_thread();
    // 同一批 revents 可能同時有 EPOLLHUP 與 EPOLLRDHUP，兩條路徑都會走到這裡；只處理一次
    if (state_ == State::kDisconnected) {
        return;
    }
    state_ = State::kDisconnected;
    channel_->disable_all();

    TcpConnectionPtr guard(shared_from_this());
    if (connection_cb_) {
        connection_cb_(guard);  // 通知使用者：連線斷了（connected() == false）
    }
    if (close_cb_) {
        close_cb_(guard);  // 通知 TcpServer：把我從 map 移除
    }
}

void TcpConnection::handle_error() {
    const int err = Socket::get_socket_error(channel_->fd());
    if (err != 0 && err != ECONNRESET && err != EPIPE) {
        std::fprintf(stderr, "TcpConnection::handle_error [%s]: %s\n", name_.c_str(),
                     std::strerror(err));
    }
}

}  // namespace tinynet

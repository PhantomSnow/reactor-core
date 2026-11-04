#pragma once

#include <any>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "tinynet/Buffer.h"
#include "tinynet/InetAddress.h"
#include "tinynet/Socket.h"

namespace tinynet {

class Channel;
class EventLoop;
class TcpConnection;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr&, std::size_t)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;

// 一條已建立的 TCP 連線。由 TcpServer 建立，屬於某一個 I/O loop。
//
// 生命週期為什麼用 shared_ptr？
//   連線可能在「使用者回呼正在執行」時被對端關閉；也可能有其他執行緒還拿著它準備 send。
//   誰最後放手誰才釋放，正是 shared_ptr 的語意。TcpServer 的 map 持有一份，
//   Channel 透過 tie() 持有 weak_ptr，處理事件時暫時升級成 shared_ptr，
//   因此 handle_close 一路呼叫到 TcpServer 把它從 map 移除時，物件也不會在半途被析構。
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(EventLoop* loop, std::string name, int sockfd, const InetAddress& local_addr,
                  const InetAddress& peer_addr);
    ~TcpConnection();

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    [[nodiscard]] EventLoop* loop() const noexcept { return loop_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const InetAddress& local_address() const noexcept { return local_addr_; }
    [[nodiscard]] const InetAddress& peer_address() const noexcept { return peer_addr_; }
    [[nodiscard]] bool connected() const noexcept { return state_ == State::kConnected; }

    // 以下皆 thread-safe：可以從任何執行緒呼叫
    void send(std::string_view data);
    void send(Buffer* buf);  // 送出後清空 buf
    void shutdown();         // 送完 output buffer 後半關閉 (SHUT_WR)
    void force_close();      // 立即關閉，丟棄尚未送出的資料

    void set_tcp_nodelay(bool on) { socket_.set_tcp_nodelay(on); }

    // 讓上層掛自己的狀態（例如 HTTP parser）
    void set_context(std::any context) { context_ = std::move(context); }
    std::any& context() noexcept { return context_; }

    void set_connection_callback(ConnectionCallback cb) { connection_cb_ = std::move(cb); }
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    void set_write_complete_callback(WriteCompleteCallback cb) { write_complete_cb_ = std::move(cb); }
    void set_high_water_mark_callback(HighWaterMarkCallback cb, std::size_t high_water_mark) {
        high_water_mark_cb_ = std::move(cb);
        high_water_mark_ = high_water_mark;
    }
    void set_close_callback(CloseCallback cb) { close_cb_ = std::move(cb); }

    Buffer* input_buffer() noexcept { return &input_buffer_; }
    Buffer* output_buffer() noexcept { return &output_buffer_; }

    // 只給 TcpServer 呼叫（在 I/O loop 執行緒中）
    void connect_established();
    void connect_destroyed();

private:
    enum class State { kConnecting, kConnected, kDisconnecting, kDisconnected };

    void handle_read();
    void handle_write();
    void handle_close();
    void handle_error();

    void send_in_loop(const char* data, std::size_t len);
    void shutdown_in_loop();
    void force_close_in_loop();

    EventLoop* loop_;
    const std::string name_;
    std::atomic<State> state_{State::kConnecting};
    Socket socket_;
    std::unique_ptr<Channel> channel_;
    const InetAddress local_addr_;
    const InetAddress peer_addr_;

    ConnectionCallback connection_cb_;
    MessageCallback message_cb_;
    WriteCompleteCallback write_complete_cb_;
    HighWaterMarkCallback high_water_mark_cb_;
    CloseCallback close_cb_;
    std::size_t high_water_mark_{64 * 1024 * 1024};

    Buffer input_buffer_;
    Buffer output_buffer_;
    std::any context_;
};

}  // namespace tinynet
